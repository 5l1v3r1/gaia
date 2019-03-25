// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "mr/pipeline.h"
#include "mr/pipeline_executor.h"

#include "base/logging.h"

namespace mr3 {
using namespace std;

Pipeline::Pipeline() {}
Pipeline::~Pipeline() {}

const InputBase& Pipeline::input(const std::string& name) const {
  for (const auto& ptr : inputs_) {
    if (ptr->msg().name() == name) {
      return *ptr;
    }
  }
  LOG(FATAL) << "Could not find " << name;
  return *inputs_.front();
}

StringTable Pipeline::ReadText(const string& name, const std::vector<std::string>& globs) {
  std::unique_ptr<InputBase> ib(new InputBase(name, pb::WireFormat::TXT));
  for (const auto& s : globs) {
    ib->mutable_msg()->add_file_spec()->set_url_glob(s);
  }
  inputs_.emplace_back(std::move(ib));

  TableImpl<std::string>::PtrType ptr(new TableImpl<std::string>(name));
  // tables_.emplace_back(ptr);

  return StringTable{ptr};
}

void Pipeline::Run(util::IoContextPool* pool, Runner* runner) {
  CHECK(!tables_.empty());
  auto ptr = tables_.front();

  executor_.reset(new Executor{pool, runner});
}

Runner::~Runner() {}

}  // namespace mr3
