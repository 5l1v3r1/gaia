// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "mr/mr.h"

#include "base/logging.h"

namespace mr3 {
using namespace std;

const InputBase& Pipeline::input(const std::string& name) const {
  for (const auto& ptr : inputs_) {
    if (ptr->msg().name() == name) {
      return *ptr;
    }
  }
  LOG(FATAL) << "Could not find " << name;
  return *inputs_.front();
}

StringStream& Pipeline::ReadText(const string& name, const string& glob) {
  return ReadText(name, {glob});
}

StringStream& Pipeline::ReadText(const string& name, const std::vector<std::string>& globs) {
  std::unique_ptr<InputBase> ib(new InputBase(name, pb::WireFormat::TXT));
  for (const auto& s : globs) {
    ib->mutable_msg()->add_file_spec()->set_url_glob(s);
  }
  inputs_.emplace_back(std::move(ib));

  streams_.emplace_back(new StringStream(name));
  auto& ptr = streams_.back();

  return *ptr;
}

RawContext::~RawContext() {}

}  // namespace mr3
