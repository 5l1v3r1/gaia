// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "util/rpc/rpc_test_utils.h"

#include "absl/strings/numbers.h"
#include "absl/strings/strip.h"
#include "base/logging.h"

#include "util/asio/client_channel.h"

using namespace std;

namespace util {
namespace rpc {

Status TestBridge::HandleEnvelope(uint64_t rpc_id, Envelope* envelope, EnvelopeWriter writer) {
  VLOG(1) << "Got " << rpc_id << ", hs=" << envelope->header.size()
          << ", ls=" << envelope->letter.size();
  if (clear_) {
    envelope->Clear();
  }
  absl::string_view header(strings::charptr(envelope->header.data()), envelope->header.size());

  if (absl::ConsumePrefix(&header, "repeat")) {
    uint32_t repeat = 0;
    CHECK(absl::SimpleAtoi(header, &repeat));
    for (uint32_t i = 0; i < repeat; ++i) {
      Envelope tmp;
      Copy(envelope->letter, &tmp.letter);
      string h = string("cont:") + to_string(i + 1 < repeat);
      Copy(h, &tmp.header);
      writer(move(tmp));
    }
  } else {
    writer(std::move(*envelope));
  }
  return Status::OK;
}

ServerTest::ServerTest() {
}

void ServerTest::SetUp() {
  pool_.reset(new IoContextPool);
  pool_->Run();
  service_.reset(new TestInterface);
  server_.reset(new AcceptServer(pool_.get()));
  uint16_t port = service_->Listen(0, server_.get());

  server_->Run();
  channel_.reset(new ClientChannel(pool_->GetNextContext(), "127.0.0.1", std::to_string(port)));
  ec_ = channel_->Connect(100);
  CHECK(!ec_) << ec_.message();
}

void ServerTest::TearDown() {
  server_.reset();
  channel_.reset();
  pool_->Stop();
}

}  // namespace rpc
}  // namespace util
