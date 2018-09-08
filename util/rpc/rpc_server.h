// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include <memory>

#include <google/protobuf/message.h>

#include "base/pod_array.h"
#include "util/asio/accept_server.h"
#include "util/status.h"
#include "strings/stringpiece.h"

namespace util {

class AcceptServer;
class IoContextPool;

namespace rpc {

typedef base::PODArray<uint8_t> BufferType;

class ConnectionBridge {
 public:
  virtual ~ConnectionBridge() {}

  // header and letter are input/output parameters.
  // HandleEnvelope reads first the input and if everything is parsed fine, it sends
  // back another header, letter pair.
  virtual ::util::Status HandleEnvelope(uint64_t rpc_id, BufferType* header,
                                        BufferType* letter) = 0;
};

class ServiceInterface {
 public:
  virtual ~ServiceInterface() {}

  // A factory method creating a handler that should handles requests for a single connection.
  // The ownership over handler is passed to the caller.
  virtual ConnectionBridge* CreateConnectionBridge() = 0;

  uint16_t Listen(uint16_t port, AcceptServer* acc_server);
};

}  // namespace rpc
}  // namespace util
