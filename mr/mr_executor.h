// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include <functional>
#include <boost/fiber/buffered_channel.hpp>
#include <boost/fiber/fiber.hpp>

#include "mr/mr.h"

#include "util/asio/io_context_pool.h"
#include "util/fibers/fiberqueue_threadpool.h"
#include "util/fibers/simple_channel.h"

namespace mr3 {

class GlobalDestFileManager {
  const std::string root_dir_;
  util::fibers_ext::FiberQueueThreadPool* fq_;
  google::dense_hash_map<StringPiece, ::file::WriteFile*> dest_files_;
  UniqueStrings str_db_;
  ::boost::fibers::mutex mu_;

  using Result = std::pair<StringPiece, ::file::WriteFile*>;

 public:
  GlobalDestFileManager(const std::string& root_dir, util::fibers_ext::FiberQueueThreadPool* fq)
      : root_dir_(root_dir), fq_(fq) {
    dest_files_.set_empty_key(StringPiece());
  }

  Result Get(StringPiece key);
};

class MyDoContext : public DoContext {
 public:
  MyDoContext(const pb::Output& out, GlobalDestFileManager* mgr);
  ~MyDoContext();

  void Write(const ShardId& shard_id, std::string&& record) final;

 private:
  struct Dest;

  google::dense_hash_map<StringPiece, Dest*> custom_shard_files_;

  GlobalDestFileManager* mgr_;
};


class Executor {
  using StringQueue = util::fibers_ext::SimpleChannel<std::string>;
  using FileNameQueue = ::boost::fibers::buffered_channel<std::string>;

  struct PerIoStruct;

 public:
  Executor(const std::string& root_dir, util::IoContextPool* pool);
  ~Executor();

  void Init();
  void Run(const InputBase* input, StringStream* ss);
  void Shutdown();

 private:
  // External, disk thread that reads files from disk and pumps data into record_q.
  // One per IO thread.
  void ProcessFiles(pb::WireFormat::Type tp);
  uint64_t ProcessText(file::ReadonlyFile* fd);

  void MapFiber(StreamBase* sb);

  std::string root_dir_;
  util::IoContextPool* pool_;
  FileNameQueue file_name_q_;

  static thread_local std::unique_ptr<PerIoStruct> per_io_;
  std::unique_ptr<util::fibers_ext::FiberQueueThreadPool> fq_pool_;
  std::unique_ptr<GlobalDestFileManager> dest_mgr_;
};

}  // namespace mr3
