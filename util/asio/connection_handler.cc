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

void ConnectionHandler::Notifier::Unlink(ConnectionHandler* item) noexcept {
  bool is_empty = false;
  {
    auto lock = Lock();
    list_->erase(ListType::s_iterator_to(*item));
    is_empty = list_->empty();
  }
  if (is_empty)
    cnd_.notify_one();
}

ConnectionHandler::ConnectionHandler(IoContext& context) noexcept : io_context_(context) {
}

ConnectionHandler::~ConnectionHandler() {
}

void ConnectionHandler::Init(socket_t&& sock, Notifier* notifier) {
  CHECK(!socket_);

  socket_.emplace(std::move(sock));
  CHECK(socket_->is_open());

  notifier_ = notifier;

  ip::tcp::no_delay nd(true);
  system::error_code ec;
  socket_->set_option(nd, ec);
  if (ec)
    LOG(ERROR) << "Could not set socket option " << ec.message() << " " << ec;

  socket_->non_blocking(true, ec);
  if (ec)
    LOG(ERROR) << "Could not make socket nonblocking " << ec.message() << " " << ec;
  is_open_ = true;
}

void ConnectionHandler::Run() {
  CHECK(notifier_);

  asio::post(socket_->get_executor(), [guard = ptr_t(this)] {
    guard->OnOpenSocket();

    // As long as fiber is running, 'this' is protected from deletion.
    fiber(&ConnectionHandler::RunInIOThread, std::move(guard)).detach();
  });
}

/*****************************************************************************
 *   fiber function per server connection
 *****************************************************************************/
void ConnectionHandler::RunInIOThread() {
  connections.Inc();

  CHECK(socket_);

  VLOG(1) << "ConnectionHandler::RunInIOThread: " << socket_->native_handle();
  system::error_code ec;

  try {
    while (is_open_) {
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

  notifier_->Unlink(this);

  connections.IncBy(-1);

  // RunInIOThread runs as lambda packaged with ptr_t guard on this. Once the lambda finishes,
  // it releases the ownership over this.
}

void ConnectionHandler::Close() {
  if (!is_open_)
    return;

  is_open_ = false;

  // socket::close() closes the underlying socket and cancels the pending operations.
  // The problem is that those operations return with ec = ok() so the flow  is not aware
  // that the socket is closed. That can lead to nasty bugs. Therefore the only place we close
  // socket is from the listener loop. Here we only signal that we are ready to close.
  if (socket_->is_open()) {
    system::error_code ec;
    VLOG(1) << "Before shutdown " << socket_->native_handle();
    socket_->cancel(ec);
    socket_->shutdown(socket_t::shutdown_both, ec);
    VLOG(1) << "After shutdown: " << ec << " " << ec.message();
  }

  io_context_.AwaitFiber([this] { OnCloseSocket(); });

  // I do not launch this task on executors thread because then it would hold guard-pointer to
  // this. If a io_context stops without running this callback, then ConnectionHandler won't
  // delete itself. This is a hack until we fix the shutdown behavior of io_context.
  // OnCloseSocket();

#if 0
  VLOG(1) << "Is open " << is_open_.load();
  // We close asynchronously via the thread that owns the socket to ensure thread-safety
  // for that connection.
  // We use intrusive ptr to increment the reference of this in order to allow
  // safe callback execution even if RunInIOThread released the ownership.
  asio::post(socket_->get_executor(), [me = ptr_t(this)] {
    // The socket might already be closed if RunInIOThread has finished running.
    if (me->socket_->is_open()) {
      system::error_code ec;
      VLOG(1) << "Before cancelling " << me->socket_->native_handle() << " " << me->is_open_.load();
    //  me->socket_->shutdown(socket_t::shutdown_receive, ec);
    //  LOG_IF(INFO, ec) << "Error closing socket " << me->socket_->native_handle()
    //                   << ": " << ec.message();

      // In case the socket is blocking a fiber lets break it.
      me->socket_->cancel(ec);
      LOG_IF(INFO, ec) << "Error canceling socket " << me->socket_->native_handle()
                       << ": " << ec.message();


    }

  });
#endif
}

void ListenerInterface::RegisterPool(IoContextPool* pool) {
  CHECK(pool_ == nullptr);
  pool_ = pool;
}

}  // namespace util
