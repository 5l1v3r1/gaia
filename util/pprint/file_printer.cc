// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
#include "util/pprint/file_printer.h"

#include "absl/strings/escaping.h"
#include "base/flags.h"
#include "base/hash.h"
#include "base/logging.h"

#include "util/pb2json.h"
#include "util/plang/plang_parser.hh"
#include "util/plang/plang_scanner.h"
#include "util/pprint/pprint_utils.h"

DEFINE_string(where, "", "boolean constraint in plang language");
DEFINE_string(schema, "",
              "Prints the schema of the underlying proto."
              "Can be either 'json' or 'proto'.");
DEFINE_bool(sizes, false, "Prints a rough estimation of the size of every field");
DEFINE_bool(json, false, "");
DEFINE_bool(raw, false, "");
DEFINE_string(sample_key, "", "");
DEFINE_int32(sample_factor, 0, "If bigger than 0 samples and outputs record once in k times");
DEFINE_bool(parallel, true, "");
DEFINE_bool(count, false, "");


namespace util {
namespace pprint {

using namespace std;
using strings::AsString;

static bool ShouldSkip(const gpb::Message& msg, const FdPath& fd_path) {
  if (FLAGS_sample_factor <= 0 || FLAGS_sample_key.empty())
    return false;
  const string* val = nullptr;
  string buf;
  auto cb = [&val, &buf](const gpb::Message& msg, const gpb::FieldDescriptor* fd, int, int) {
    const gpb::Reflection* refl = msg.GetReflection();
    val = &refl->GetStringReference(msg, fd, &buf);
  };
  fd_path.ExtractValue(msg, cb);
  CHECK(val);

  uint32 num = base::Fingerprint(*val);

  return (num % FLAGS_sample_factor) != 0;
}

class FilePrinter::PrintTask {
 public:
  typedef PrintSharedData* SharedData;

  void InitShared(SharedData d) {
    shared_data_ = d;
  }

  void operator()(const std::string& obj) {
    if (FLAGS_raw) {
      std::lock_guard<mutex> lock(shared_data_->m);
      std::cout << absl::Utf8SafeCEscape(obj) << "\n";
      return;
    }
    CHECK(local_msg_->ParseFromString(obj));
    if (shared_data_->expr && !plang::EvaluateBoolExpr(*shared_data_->expr, *local_msg_))
      return;

    if (ShouldSkip(*local_msg_, fd_path_))
      return;
    std::lock_guard<mutex> lock(shared_data_->m);

    if (FLAGS_sizes) {
      shared_data_->size_summarizer->AddSizes(*local_msg_);
      return;
    }

    if (FLAGS_json) {
      Pb2JsonOptions options;
      options.enum_as_ints = true;
      string str = Pb2Json(*local_msg_, options);
      std::cout << str << "\n";
    } else {
      shared_data_->printer->Output(*local_msg_);
    }
  }

  explicit PrintTask(const gpb::Message* to_clone) {
    if (to_clone) {
      local_msg_.reset(to_clone->New());
    }
    if (!FLAGS_sample_key.empty()) {
      fd_path_ = FdPath(to_clone->GetDescriptor(), FLAGS_sample_key);
      CHECK(!fd_path_.IsRepeated());
      CHECK_EQ(gpb::FieldDescriptor::CPPTYPE_STRING, fd_path_.path().back()->cpp_type());
    }
  }

 private:
  std::unique_ptr<gpb::Message> local_msg_;
  FdPath fd_path_;
  SharedData shared_data_;
};

FilePrinter::FilePrinter() {}
FilePrinter::~FilePrinter() {}

void FilePrinter::Init(const string& fname) {
  CHECK(!descr_msg_);

  if (!FLAGS_where.empty()) {
    std::istringstream istr(FLAGS_where);
    plang::Scanner scanner(&istr);
    plang::Parser parser(&scanner, &test_expr_);
    CHECK_EQ(0, parser.parse()) << "Could not parse " << FLAGS_where;
  }

  LoadFile(fname);

  if (descr_msg_) {
    if (!FLAGS_schema.empty()) {
      if (FLAGS_schema == "json") {
        PrintBqSchema(descr_msg_->GetDescriptor());
      } else if (FLAGS_schema == "proto") {
        cout << descr_msg_->GetDescriptor()->DebugString() << std::endl;
      } else {
        LOG(FATAL) << "Unknown schema";
      }
      exit(0);  // Geez!.
    }

    if (FLAGS_sizes)
      size_summarizer_.reset(new SizeSummarizer(descr_msg_->GetDescriptor()));
    printer_.reset(new Printer(descr_msg_->GetDescriptor()));
  } else {
    CHECK(!FLAGS_sizes && FLAGS_schema.empty());
  }

  pool_.reset(new TaskPool("pool", 10));

  shared_data_.size_summarizer = size_summarizer_.get();
  shared_data_.printer = printer_.get();
  shared_data_.expr = test_expr_.get();
  pool_->SetSharedData(&shared_data_);
  pool_->Launch(descr_msg_.get());

  if (FLAGS_parallel) {
    LOG(INFO) << "Running in parallel " << pool_->thread_count() << " threads";
  }
}

Status FilePrinter::Run() {
  StringPiece record;
  while (true) {
    util::StatusObject<bool> res = Next(&record);
    if (!res.ok())
      return res.status;
    if (!res.obj)
      break;
    if (FLAGS_count) {
      ++count_;
    } else {
      if (FLAGS_parallel) {
        pool_->RunTask(AsString(record));
      } else {
        pool_->RunInline(AsString(record));
      }
    }
  }
  pool_->WaitForTasksToComplete();

  if (size_summarizer_.get())
    std::cout << *size_summarizer_ << "\n";
  return Status::OK;
}

}  // namespace pprint
}  // namespace util
