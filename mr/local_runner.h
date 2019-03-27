// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include "mr/pipeline.h"

namespace mr3 {

class LocalRunner : public Runner {
 public:
  LocalRunner(const std::string& data_dir);
  ~LocalRunner();

  void Init() final;

  void Shutdown() final;

  RawContext* CreateContext(const pb::Operator& op) final;

  void ExpandGlob(const std::string& glob, std::function<void(const std::string&)> cb) final;

  // Read file and fill queue. This function must be fiber-friendly.
  void ProcessFile(const std::string& filename, pb::WireFormat::Type type,
                   RecordQueue* queue) final;
 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};


}  // namespace mr3
