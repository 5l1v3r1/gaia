// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include "base/object_pool.h"

#include "util/asio/io_context.h"
#include "util/asio/connection_handler.h"
#include "util/rpc/frame_format.h"
#include "util/rpc/rpc_connection.h"

namespace util {
namespace rpc {

using namespace boost;

// Generally it's an object called from a single fiber.
// However FlushFiber runs from a background fiber and makes sure that all outgoing writes
// are flushed to socket.
class RpcConnectionHandler : public ConnectionHandler {
  connection_hook_t flush_hook_;

 public:
  // bridge is owned by RpcConnectionHandler instance.
  // RpcConnectionHandler is created in acceptor thread and not in the socket thread.
  RpcConnectionHandler(ConnectionBridge* bridge, IoContext* context);
  ~RpcConnectionHandler();

  system::error_code HandleRequest() final override;

  using rpc_hook_t = detail::member_hook<RpcConnectionHandler, detail::connection_hook,
                                        &RpcConnectionHandler::flush_hook_> ;

  // Called by Flusher fiber to flush the outgoing writes.
  void PollAndFlushWrites() {
    if (!is_open_ || !socket_->is_open() || outgoing_buf_.empty())
      return;
    FlushWrites();
  }

 private:
  // protected by wr_mu_ to preserve transcational semantics:
  void FlushWrites();

  // The following methods are run in the socket thread (thread that calls HandleRequest.)
  void OnOpenSocket() final;
  void OnCloseSocket() final;
  bool ShouldFlush();

  using socket_t = asio::ip::tcp::socket;

  std::unique_ptr<ConnectionBridge> bridge_;

  struct RpcItem : public intrusive::slist_base_hook<intrusive::link_mode<intrusive::normal_link>> {
    RpcId id;
    Envelope envelope;

    RpcItem() = default;
    RpcItem(RpcId i, Envelope env) : id(i), envelope(std::move(env)) {
    }

    auto buf_seq() {
      return envelope.buf_seq();
    }
  };
  using ItemList = intrusive::slist<RpcItem, intrusive::cache_last<true>>;

  system::error_code ec_;
  base::ObjectPool<RpcItem> rpc_items_;
  ItemList outgoing_buf_;

  fibers::mutex wr_mu_;
  std::vector<asio::const_buffer> write_seq_;
  base::PODArray<std::array<uint8_t, rpc::Frame::kMaxByteSize>> frame_buf_;
};

}  // namespace rpc
}  // namespace util
