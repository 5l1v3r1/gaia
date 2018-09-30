// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include "base/RWSpinLock.h"  //
#include "util/asio/client_channel.h"
#include "util/rpc/buffered_read_adaptor.h"
#include "util/rpc/frame_format.h"
#include "util/rpc/rpc_envelope.h"
#include "absl/container/flat_hash_map.h"

namespace util {
namespace rpc {

// Fiber-safe rpc client.
// Send(..) is also thread-safe and may be used from multiple threads differrent than
// of IoContext containing the channel, however it might incur performance penalty.
// Therefore to achieve maximal performance - it's advised to ClientBase only from IoContext thread.
class ClientBase {
 public:
  using error_code = ClientChannel::error_code;
  using future_code_t = boost::fibers::future<error_code>;

  ClientBase(ClientChannel&& channel) : channel_(std::move(channel)), br_(channel_.socket(), 2048) {
  }

  ClientBase(IoContext& cntx, const std::string& hostname, const std::string& service)
      : ClientBase(ClientChannel(cntx, hostname, service)) {
  }

  ~ClientBase();

  // Blocks at least for 'ms' milliseconds to connect to the host.
  // Should be called once during the initialization phase before sending the requests.
  error_code Connect(uint32_t ms);

  // Thread-safe function.
  // Sends the envelope and returns the future to the response status code.
  // Future is realized when response is received and serialized into the same envelope.
  // Send() might block therefore it should not be called directly from IoContext loop (post).
  future_code_t Send(Envelope* envelope);

  // Fiber-blocking call. Sends and waits until the response is back.
  // Similarly to Send, the response is written into the same envelope.
  error_code SendSync(Envelope* envelope) {
    return Send(envelope).get();
  }

  // Blocks the calling fiber until all the background processes finish.
  void Shutdown();

 private:
  void ReadFiber();
  void FlushFiber();

  void CancelPendingCalls(error_code ec);
  error_code ReadEnvelope();
  error_code PresendChecks();
  error_code FlushSends();
  error_code FlushSendsGuarded();

  void CancelSentBufferGuarded(error_code ec);

  RpcId rpc_id_ = 1;
  ClientChannel channel_;
  BufferedReadAdaptor<ClientChannel::socket_t> br_;
  typedef boost::fibers::promise<error_code> EcPromise;

  struct PendingCall {
    EcPromise promise;
    Envelope* envelope;

    PendingCall(EcPromise p, Envelope* env)
        : promise(std::move(p)), envelope(env) {
    }
  };

  typedef std::pair<RpcId, PendingCall> SendItem;

  typedef absl::flat_hash_map<RpcId, PendingCall> PendingMap;
  PendingMap pending_calls_;
  std::atomic_ulong pending_calls_size_{0};

  folly::RWSpinLock buf_lock_;
  std::vector<SendItem> outgoing_buf_;  // protected by buf_lock_.
  std::atomic_ulong outgoing_buf_size_{0};

  boost::fibers::fiber read_fiber_, flush_fiber_;
  boost::fibers::mutex send_mu_;
  std::vector<boost::asio::const_buffer> write_seq_;
  base::PODArray<std::array<uint8_t, rpc::Frame::kMaxByteSize>> frame_buf_;
};

}  // namespace rpc
}  // namespace util
