// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include <functional>
#include <string>

#include "strings/stringpiece.h"
#include "util/plang/plang.h"
#include "util/sp_task_pool.h"
#include "util/status.h"

namespace file {
class ListReader;
} // namespace file

namespace util {
namespace pprint {

class SizeSummarizer;
class Printer;

class FilePrinter {
 public:
  using FieldNameCb = std::function<std::string(const ::google::protobuf::FieldOptions& fo,
                                                const ::google::protobuf::FieldDescriptor& fd)>;

  FilePrinter();
  virtual ~FilePrinter();

  void Init(const std::string& fname);
  util::Status Run();

  uint64_t count() const {
    return count_;
  }

  template<typename Func> void set_fieldname_cb(Func&& f) {
    name_cb_ = f;
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
  FieldNameCb name_cb_;
  uint64_t count_ = 0;
};

class ListReaderPrinter final : public FilePrinter {
 protected:
  void LoadFile(const std::string& fname) override;
  util::StatusObject<bool> Next(StringPiece* record) override;
  void PostRun() override;

 private:
  std::unique_ptr<file::ListReader> reader_;
  std::string record_buf_;
  util::Status st_;
};

}  // namespace pprint
}  // namespace util
