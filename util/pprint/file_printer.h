// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include <string>

#include "strings/stringpiece.h"
#include "util/plang/plang.h"
#include "util/sp_task_pool.h"
#include "util/status.h"

namespace util {
namespace pprint {

class SizeSummarizer;
class Printer;

class FilePrinter {
 public:
  FilePrinter();
  virtual ~FilePrinter();

  void Init(const std::string& fname);
  util::Status Run();

  uint64_t count() const {
    return count_;
  }

 protected:
  virtual void LoadFile(const std::string& fname) = 0;

  // Returns false if EOF reached, true if Next call succeeded and status code overthise.
  virtual util::StatusObject<bool> Next(StringPiece* record) = 0;

  virtual void PostRun() {
  }

  std::unique_ptr<const ::google::protobuf::Message> descr_msg_;

 private:
  class PrintTask;

  struct PrintSharedData {
    std::mutex m;
    const plang::Expr* expr = nullptr;
    const Printer* printer = nullptr;
    SizeSummarizer* size_summarizer = nullptr;
  };


  using TaskPool = util::SingleProducerTaskPool<PrintTask>;

  std::unique_ptr<TaskPool> pool_;
  std::unique_ptr<Printer> printer_;
  std::unique_ptr<SizeSummarizer> size_summarizer_;
  std::unique_ptr<plang::Expr> test_expr_;

  PrintSharedData shared_data_;
  uint64_t count_ = 0;
};

}  // namespace pprint
}  // namespace util
