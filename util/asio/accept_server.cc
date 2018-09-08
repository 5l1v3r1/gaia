// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "util/asio/accept_server.h"

#include <boost/fiber/mutex.hpp>

#include "base/logging.h"
#include "util/asio/io_context_pool.h"
#include "util/asio/yield.h"

namespace util {

using namespace boost;
using asio::ip::tcp;

AcceptServer::AcceptServer(IoContextPool* pool)
     :pool_(pool), signals_(pool->GetNextContext(), SIGINT, SIGTERM), bc_(1) {

  signals_.async_wait(
  [this](system::error_code /*ec*/, int /*signo*/) {
    // The server is stopped by cancelling all outstanding asynchronous
    // operations. Once all operations have finished the io_context::run()
    // call will exit.
    for (auto& l : listeners_)
      l.acceptor.close();

    bc_.Dec();
  });
}

AcceptServer::~AcceptServer() {
  Stop();
  Wait();
}

unsigned short AcceptServer::AddListener(unsigned short port, ConnectionFactory cf) {
  CHECK(!was_run_);

  tcp::endpoint endpoint(tcp::v4(), port);
  listeners_.emplace_back(&pool_->GetNextContext(), endpoint, std::move(cf));
  auto& listener = listeners_.back();

  LOG(INFO) << "AcceptServer - listening on port " << listener.port;

  return listener.port;
}

void AcceptServer::RunInIOThread(Listener* listener) {
  util::ConnectionHandler::ListType clist;

  // wrap it to allow thread-safe and consistent access to the list.
  ConnectionHandler::Notifier notifier(&clist);

  system::error_code ec;
  util::ConnectionHandler* handler = nullptr;
  try {
    for (;;) {
       std::tie(handler,ec) = AcceptFiber(listener, &notifier);
       if (ec) {
         CHECK(!handler);
         break; // TODO: To refine it.
      } else {
        VLOG(1) << "Accepted socket " << handler->socket().remote_endpoint();
        CHECK_NOTNULL(handler);
        clist.push_back(*handler);
        DCHECK(!clist.empty());

        DCHECK(handler->hook_.is_linked());
        handler->Run();
      }
    }
  } catch (std::exception const& ex) {
    LOG(WARNING) << ": caught exception : " << ex.what();
  }

  // We protect clist because we iterate over it and other threads could potentialy change it.
  // connections are dispersed across few threads so clist requires true thread synchronization.
  auto lock = notifier.Lock();

  if (!clist.empty()) {
    VLOG(1) << "Closing " << clist.size() << " connections";

    for (auto it = clist.begin(); it != clist.end(); ++it) {
      it->Close();
    }

    VLOG(1) << "Waiting for connections to close";
    notifier.WaitTillEmpty(lock);
  }

  // Notify that AcceptThread has stopped.
  bc_.Dec();

  LOG(INFO) << "Accept server stopped";
}

auto AcceptServer::AcceptFiber(Listener* listener, ConnectionHandler::Notifier* notifier)
   -> AcceptResult {
  auto& io_cntx = pool_->GetNextContext();

  system::error_code ec;
  tcp::socket sock(io_cntx);
  listener->acceptor.async_accept(sock, fibers_ext::yield[ec]);

  if (ec)
    return AcceptResult(nullptr, ec);
  ConnectionHandler* conn = listener->cf();
  conn->Init(std::move(sock), notifier);

  return AcceptResult(conn, ec);
}


void AcceptServer::Run() {
  CHECK(!listeners_.empty());

  bc_.Add(listeners_.size());

  for (auto& listener : listeners_) {
    Listener* ptr = &listener;

    io_context& io_cntx = ptr->acceptor.get_executor().context();
    asio::post(io_cntx, [this, ptr] {
      fibers::fiber srv_fb(&AcceptServer::RunInIOThread, this, ptr);
      srv_fb.detach();
    });
  }
  was_run_ = true;
}

void AcceptServer::Wait() {
  if (was_run_)
    bc_.Wait();
}

}  // namespace util
