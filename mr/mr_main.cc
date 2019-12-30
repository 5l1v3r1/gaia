// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "mr/mr_main.h"

#include "base/init.h"

#include "file/file_util.h"

#include "mr/local_runner.h"
#include "util/asio/accept_server.h"
#include "util/asio/io_context_pool.h"
#include "util/sentry/sentry.h"

namespace mr3 {

DEFINE_int32(http_port, 8080, "Port number.");

using namespace util;

PipelineMain::PipelineMain() {
  Init();
}

PipelineMain::PipelineMain(int* argc, char*** argv) : guard_(new MainInitGuard{argc, argv}) {
  Init();
}

void PipelineMain::Init() {
  pool_.reset(new IoContextPool);
  pool_->Run();
  util::EnableSentry(&pool_->GetNextContext());
  pipeline_.reset(new Pipeline(pool_.get()));

  acc_server_.reset(new AcceptServer(pool_.get()));
  if (FLAGS_http_port >= 0) {
    uint16_t port = acc_server_->AddListener(FLAGS_http_port, &http_listener_);
    LOG(INFO) << "Started http server on port " << port;
  }
  acc_server_->Run();
}

PipelineMain::~PipelineMain() {
  acc_server_->Stop(true);
  pool_->Stop();
}

LocalRunner* PipelineMain::StartLocalRunner(const std::string& root_dir, bool stop_on_break) {
  CHECK(!runner_);
  runner_.reset(new LocalRunner(pool_.get(), file_util::ExpandPath(root_dir)));
  if (stop_on_break) {
    acc_server_->TriggerOnBreakSignal([this] {
      pipeline_->Stop();
      runner_->Stop();
    });
  }
  return runner_.get();
}

}  // namespace mr3
