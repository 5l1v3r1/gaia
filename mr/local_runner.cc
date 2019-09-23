// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "mr/local_runner.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "base/histogram.h"
#include "base/logging.h"
#include "base/walltime.h"

#include "file/fiber_file.h"
#include "file/file_util.h"
#include "file/filesource.h"
#include "file/list_file_reader.h"

#include "mr/do_context.h"
#include "mr/impl/local_context.h"

#include "util/asio/io_context_pool.h"
#include "util/fibers/fiberqueue_threadpool.h"
#include "util/gce/gcs.h"
#include "util/gce/gcs_read_file.h"
#include "util/http/https_client_pool.h"

#include "util/stats/varz_stats.h"
#include "util/zlib_source.h"

namespace mr3 {

DEFINE_uint32(local_runner_prefetch_size, 1 << 16, "File input prefetch size");
DECLARE_uint32(gcs_connect_deadline_ms);

using namespace util;
using namespace boost;
using namespace std;
using detail::DestFileSet;

namespace {

using namespace intrusive;

}  // namespace

ostream& operator<<(ostream& os, const file::FiberReadOptions::Stats& stats) {
  os << stats.cache_bytes << "/" << stats.disk_bytes << "/" << stats.read_prefetch_cnt << "/"
     << stats.preempt_cnt;
  return os;
}

struct LocalRunner::Impl {
 public:
  Impl(IoContextPool* p, const string& d)
      : io_pool_(p), data_dir(d), fq_pool_(0, 128),
        varz_stats_("local-runner", [this] { return GetStats(); }) {}

  uint64_t ProcessText(file::ReadonlyFile* fd, RawSinkCb cb);
  uint64_t ProcessLst(file::ReadonlyFile* fd, RawSinkCb cb);

  /// Called from the main thread orchestrating the pipeline run.
  void Start(const pb::Operator* op);

  /// Called from the main thread orchestrating the pipeline run.
  void End(ShardFileMap* out_files);

  /// The functions below are called from IO threads.
  void ExpandGCS(absl::string_view glob, ExpandCb cb);

  StatusObject<file::ReadonlyFile*> OpenLocalFile(const std::string& filename,
                                                  file::FiberReadOptions::Stats* stats);

  StatusObject<file::ReadonlyFile*> OpenGcsFile(const std::string& filename);
  void ShutDown();

  RawContext* NewContext();

  void Break() { stop_signal_.store(true, std::memory_order_seq_cst); }
  void UpdateLocalStats(const file::FiberReadOptions::Stats& stats);

 private:
  void LazyGcsInit();
  util::VarzValue::Map GetStats() const;

  IoContextPool* io_pool_;
  string data_dir;
  fibers_ext::FiberQueueThreadPool fq_pool_;
  std::atomic_bool stop_signal_{false};
  std::atomic_ulong file_cache_hit_bytes_{0};
  const pb::Operator* current_op_ = nullptr;

  fibers::mutex gce_mu_;
  std::unique_ptr<GCE> gce_handle_;


  struct PerThread {
    vector<unique_ptr<GCS>> gcs_handles;

    base::Histogram record_fetch_hist;

    absl::optional<asio::ssl::context> ssl_context;
    absl::optional<http::HttpsClientPool> api_conn_pool;

    void SetupGce(IoContext* io_context);
  };

  struct handle_keeper {
    PerThread* per_thread;

    handle_keeper(PerThread* pt) : per_thread(pt) {}

    void operator()(GCS* gcs) { per_thread->gcs_handles.emplace_back(gcs); }
  };

  unique_ptr<GCS, handle_keeper> GetGcsHandle();

  static thread_local std::unique_ptr<PerThread> per_thread_;
  util::VarzFunction varz_stats_;

  mutable std::mutex dest_mgr_mu_;
  std::unique_ptr<DestFileSet> dest_mgr_;
};

thread_local std::unique_ptr<LocalRunner::Impl::PerThread> LocalRunner::Impl::per_thread_;

void LocalRunner::Impl::PerThread::SetupGce(IoContext* io_context) {
  if (ssl_context) {
    return;
  }
  CHECK(io_context);

  ssl_context = GCE::CheckedSslContext();
  api_conn_pool.emplace(GCE::kApiDomain, &ssl_context.value(), io_context);
}

auto LocalRunner::Impl::GetGcsHandle() -> unique_ptr<GCS, handle_keeper> {
  auto* pt = per_thread_.get();
  CHECK(pt);
  VLOG(1) << "GetGcsHandle: " << pt->gcs_handles.size();

  CHECK(pt->ssl_context.has_value());

  for (auto it = pt->gcs_handles.begin(); it != pt->gcs_handles.end(); ++it) {
    if ((*it)->IsBusy()) {
      continue;
    }

    auto res = std::move(*it);
    it->swap(pt->gcs_handles.back());
    pt->gcs_handles.pop_back();

    return unique_ptr<GCS, handle_keeper>(res.release(), pt);
  }

  IoContext* io_context = io_pool_->GetThisContext();
  CHECK(io_context) << "Must run from IO context thread";

  GCS* gcs = new GCS(*gce_handle_, &pt->ssl_context.value(), io_context);
  CHECK_STATUS(gcs->Connect(FLAGS_gcs_connect_deadline_ms));

  return unique_ptr<GCS, handle_keeper>(gcs, pt);
}

VarzValue::Map LocalRunner::Impl::GetStats() const {
  VarzValue::Map map;
  std::atomic<uint32_t> input_gcs_conn{0};

  auto start = base::GetMonotonicMicrosFast();
  unsigned out_gcs_count = 0;
  {
    lock_guard<std::mutex> lk(dest_mgr_mu_);
    if (dest_mgr_) {
      out_gcs_count = dest_mgr_->HandleCount();
    }
  }

  io_pool_->AwaitOnAll([&](IoContext&) {
    auto* pt = per_thread_.get();
    if (pt) {
      input_gcs_conn += pt->gcs_handles.size();
    }
  });

  map.emplace_back("input-gcs-connections", VarzValue::FromInt(input_gcs_conn.load()));
  map.emplace_back("output-gcs-connections", VarzValue::FromInt(out_gcs_count));
  map.emplace_back("stats-latency", VarzValue::FromInt(base::GetMonotonicMicrosFast() - start));

  return map;
}

uint64_t LocalRunner::Impl::ProcessText(file::ReadonlyFile* fd, RawSinkCb cb) {
  std::unique_ptr<util::Source> src(file::Source::Uncompressed(fd));

  file::LineReader lr(src.release(), TAKE_OWNERSHIP);
  StringPiece result;
  string scratch;

  uint64_t cnt = 0;
  uint64_t start = base::GetMonotonicMicrosFast();
  while (!stop_signal_.load(std::memory_order_relaxed) && lr.Next(&result, &scratch)) {
    string tmp{result};
    ++cnt;
    if (VLOG_IS_ON(1)) {
      int64_t delta = base::GetMonotonicMicrosFast() - start;
      if (delta > 5)  // Filter out uninteresting fast Next calls.
        per_thread_->record_fetch_hist.Add(delta);
    }
    VLOG_IF(2, cnt % 1000 == 0) << "Read " << cnt << " items";
    if (cnt % 1000 == 0) {
      this_fiber::yield();
    }
    cb(std::move(tmp));
    start = base::GetMonotonicMicrosFast();
  }
  VLOG(1) << "ProcessText Read " << cnt << " items";

  return cnt;
}

uint64_t LocalRunner::Impl::ProcessLst(file::ReadonlyFile* fd, RawSinkCb cb) {
  file::ListReader::CorruptionReporter error_fn = [](size_t bytes, const util::Status& status) {
    LOG(FATAL) << "Lost " << bytes << " bytes, status: " << status;
  };

  file::ListReader list_reader(fd, TAKE_OWNERSHIP, true, error_fn);
  string scratch;
  StringPiece record;
  uint64_t cnt = 0;
  while (list_reader.ReadRecord(&record, &scratch)) {
    cb(string(record));
    ++cnt;
    if (cnt % 1000 == 0) {
      this_fiber::yield();
      if (stop_signal_.load(std::memory_order_relaxed)) {
        break;
      }
    }
  }
  return cnt;
}

void LocalRunner::Impl::Start(const pb::Operator* op) {
  CHECK(!dest_mgr_);
  current_op_ = op;
  string out_dir = file_util::JoinPath(data_dir, op->output().name());
  if (util::IsGcsPath(out_dir)) {
    LazyGcsInit();  // Initializes gce handle.
  } else if (!file::Exists(out_dir)) {
    CHECK(file_util::RecursivelyCreateDir(out_dir, 0750)) << "Could not create dir " << out_dir;
  }

  lock_guard<mutex> lk(dest_mgr_mu_);
  dest_mgr_.reset(new DestFileSet(out_dir, op->output(), io_pool_, &fq_pool_));

  if (util::IsGcsPath(out_dir)) {
    dest_mgr_->set_gce(gce_handle_.get());
  }
}

void LocalRunner::Impl::End(ShardFileMap* out_files) {
  CHECK(dest_mgr_);

  auto shards = dest_mgr_->GetShards();
  for (const ShardId& sid : shards) {
    out_files->emplace(sid, dest_mgr_->ShardFilePath(sid, -1));
  }
  dest_mgr_->CloseAllHandles(stop_signal_.load(std::memory_order_acquire));

  lock_guard<mutex> lk(dest_mgr_mu_);
  dest_mgr_.reset();
  current_op_ = nullptr;
}

void LocalRunner::Impl::ExpandGCS(absl::string_view glob, ExpandCb cb) {
  absl::string_view bucket, path;
  CHECK(GCS::SplitToBucketPath(glob, &bucket, &path));

  // Lazy init of gce_handle.
  LazyGcsInit();

  auto cb2 = [cb = std::move(cb), bucket](size_t sz, absl::string_view s) {
    cb(sz, GCS::ToGcsPath(bucket, s));
  };
  bool recursive = absl::EndsWith(glob, "**");
  if (recursive) {
    path.remove_suffix(2);
  } else if (absl::EndsWith(glob, "*")) {
    path.remove_suffix(1);
  }

  auto gcs = GetGcsHandle();
  bool fs_mode = !recursive;
  auto status = gcs->List(bucket, path, fs_mode, cb2);
  CHECK_STATUS(status);
}

StatusObject<file::ReadonlyFile*> LocalRunner::Impl::OpenGcsFile(const std::string& filename) {
  CHECK(IsGcsPath(filename));
  if (!per_thread_) {
    per_thread_.reset(new PerThread);
  }

  LazyGcsInit();

  auto gcs = GetGcsHandle();
  return gcs->OpenGcsFile(filename);
}

StatusObject<file::ReadonlyFile*> LocalRunner::Impl::OpenLocalFile(
    const std::string& filename, file::FiberReadOptions::Stats* stats) {
  if (!per_thread_) {
    per_thread_.reset(new PerThread);
  }
  CHECK(!IsGcsPath(filename));

  file::FiberReadOptions opts;
  opts.prefetch_size = FLAGS_local_runner_prefetch_size;
  opts.stats = stats;

  return file::OpenFiberReadFile(filename, &fq_pool_, opts);
}

void LocalRunner::Impl::LazyGcsInit() {
  if (!per_thread_) {
    per_thread_.reset(new PerThread);
  }
  per_thread_->SetupGce(io_pool_->GetThisContext());

  {
    std::lock_guard<fibers::mutex> lk(gce_mu_);
    if (!gce_handle_) {
      gce_handle_.reset(new GCE);
      CHECK_STATUS(gce_handle_->Init());
    }
  }
}

void LocalRunner::Impl::ShutDown() {
  fq_pool_.Shutdown();

  auto cb_per_thread = [this](IoContext& ) {
    if (per_thread_) {
      auto pt = per_thread_.get();
      VLOG(1) << "Histogram Latency: " << pt->record_fetch_hist.ToString();

      per_thread_.reset();
    }
  };

  io_pool_->AwaitFiberOnAll(cb_per_thread);

  auto cached_bytes = file_cache_hit_bytes_.load();
  LOG_IF(INFO, cached_bytes) << "File cached hit bytes " << cached_bytes;
}

RawContext* LocalRunner::Impl::NewContext() {
  CHECK_NOTNULL(current_op_);

  return new detail::LocalContext(dest_mgr_.get());
}

void LocalRunner::Impl::UpdateLocalStats(const file::FiberReadOptions::Stats& stats) {
  VLOG(1) << "Read Stats (disk read/cached/read_cnt/preempts): " << stats;

  file_cache_hit_bytes_.fetch_add(stats.cache_bytes, std::memory_order_relaxed);
}

/* LocalRunner implementation
********************************************/

LocalRunner::LocalRunner(IoContextPool* pool, const std::string& data_dir)
    : impl_(new Impl(pool, data_dir)) {}

LocalRunner::~LocalRunner() {}

void LocalRunner::Init() {
}

void LocalRunner::Shutdown() {
  impl_->ShutDown();
}

void LocalRunner::OperatorStart(const pb::Operator* op) { impl_->Start(op); }

RawContext* LocalRunner::CreateContext() { return impl_->NewContext(); }

void LocalRunner::OperatorEnd(ShardFileMap* out_files) {
  VLOG(1) << "LocalRunner::OperatorEnd";
  impl_->End(out_files);
}

void LocalRunner::ExpandGlob(const std::string& glob, ExpandCb cb) {
  if (util::IsGcsPath(glob)) {
    impl_->ExpandGCS(glob, cb);
    return;
  }

  std::vector<file_util::StatShort> paths = file_util::StatFiles(glob);
  for (const auto& v : paths) {
    if (v.st_mode & S_IFREG) {
      cb(v.size, v.name);
    }
  }
}


// Read file and fill queue. This function must be fiber-friendly.
size_t LocalRunner::ProcessInputFile(const std::string& filename, pb::WireFormat::Type type,
                                     RawSinkCb cb) {
  file::FiberReadOptions::Stats stats;
  bool is_gcs = IsGcsPath(filename);

  StatusObject<file::ReadonlyFile*> fl_res;
  if (is_gcs) {
    fl_res = impl_->OpenGcsFile(filename);
  } else {
    fl_res = impl_->OpenLocalFile(filename, &stats);
  }

  if (!fl_res.ok()) {
    LOG(FATAL) << "Skipping " << filename << " with " << fl_res.status;
    return 0;
  }

  LOG(INFO) << "Processing file " << filename;
  std::unique_ptr<file::ReadonlyFile> read_file(fl_res.obj);
  size_t cnt = 0;
  switch (type) {
    case pb::WireFormat::TXT:
      cnt = impl_->ProcessText(read_file.release(), cb);
      break;
    case pb::WireFormat::LST:
      cnt = impl_->ProcessLst(read_file.release(), cb);
      break;
    default:
      LOG(FATAL) << "Not implemented " << pb::WireFormat::Type_Name(type);
      break;
  }

  if (!is_gcs)
    impl_->UpdateLocalStats(stats);

  return cnt;
}

void LocalRunner::Stop() {
  CHECK_NOTNULL(impl_)->Break();
}

}  // namespace mr3
