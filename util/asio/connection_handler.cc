// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "util/asio/connection_handler.h"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/write.hpp>

#include "base/logging.h"
#include "util/asio/io_context.h"
#include "util/stats/varz_stats.h"

using namespace boost;
using namespace boost::asio;
using fibers::condition_variable;
using fibers::fiber;
using namespace std;

DEFINE_VARZ(VarzCount, connections);

namespace util {

bool IsExpectedFinish(system::error_code ec) {
  return ec == error::eof || ec == error::operation_aborted || ec == error::connection_reset ||
         ec == error::not_connected;
}

ConnectionHandler::ConnectionHandler(IoContext* context) noexcept : io_context_(*context) {
  CHECK_NOTNULL(context);
}

ConnectionHandler::~ConnectionHandler() {
}

void ConnectionHandler::Init(asio::ip::tcp::socket&& sock) {
  CHECK(!socket_ && sock.is_open());
  ip::tcp::no_delay nd(true);
  system::error_code ec;
  sock.set_option(nd, ec);
  if (ec)
    LOG(ERROR) << "Could not set socket option " << ec.message() << " " << ec;

  sock.non_blocking(true, ec);
  if (ec)
    LOG(ERROR) << "Could not make socket nonblocking " << ec.message() << " " << ec;

  socket_.emplace(std::move(sock));
  CHECK(socket_->is_open());
}

/*****************************************************************************
 *   fiber function per server connection
 *****************************************************************************/
void ConnectionHandler::RunInIOThread() {
  DCHECK(io_context_.InContextThread());

  connections.Inc();

  CHECK(socket_);
  OnOpenSocket();

  VLOG(1) << "ConnectionHandler::RunInIOThread: " << socket_->native_handle();
  system::error_code ec;

  try {
    while (socket_->is_open()) {
      ec = HandleRequest();
      if (ec) {
        if (!IsExpectedFinish(ec)) {
          LOG(WARNING) << "Error : " << ec.message() << ", " << ec.category().name() << "/"
                       << ec.value();
        }
        break;
      }
    }
    VLOG(1) << "ConnectionHandler closed: " << socket_->native_handle();
  } catch (std::exception const& ex) {
    string str = ex.what();
    LOG(ERROR) << str;
  }

  Close();

  connections.IncBy(-1);

  // RunInIOThread runs as lambda packaged with ptr_t guard on this. Once the lambda finishes,
  // it releases the ownership over this.
}

void ConnectionHandler::Close() {
  // Run Listener hook in the connection thread.
  io_context_.AwaitSafe([this] {
    if (!socket_->is_open())
      return;

    system::error_code ec;
    VLOG(1) << "Before shutdown " << socket_->native_handle();
    socket_->Shutdown(ec);
    VLOG(1) << "After shutdown: " << ec << " " << ec.message();
    // socket::close() closes the underlying socket and cancels the pending operations.
    // HOWEVER the problem is that those operations return with ec = ok()
    // so the flow  is not aware that the socket is closed.
    // That can lead to nasty bugs. Therefore the only place we close
    // socket is from the listener loop. Here we only signal that we are ready to close.
    OnCloseSocket();
  });
}

void ListenerInterface::RegisterPool(IoContextPool* pool) {
  // In tests we might relaunch AcceptServer with the same listener, so we allow
  // reassigning the same pool.
  CHECK(pool_ == nullptr || pool_ == pool);
  pool_ = pool;
}

}  // namespace util
