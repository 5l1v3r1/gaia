// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include <tuple>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>

#include "util/asio/connection_handler.h"
#include "util/fibers_ext.h"

namespace util {

class IoContextPool;

class AcceptServer {
 public:
  typedef ::boost::asio::io_context io_context;
  typedef ::boost::fibers::condition_variable condition_variable;

  explicit AcceptServer(IoContextPool* pool);
  ~AcceptServer();

  void Run();

  void Stop() {
    // No need to close acceptor because signals.cancel will trigger its callback that
    // will close it anyway.
    signals_.cancel();
  }

  void Wait();

  // Returns the port number to which the listener was bound.
  unsigned short AddListener(unsigned short port, ListenerInterface* cf);

 private:
  using acceptor = ::boost::asio::ip::tcp::acceptor;
  using endpoint = ::boost::asio::ip::tcp::endpoint;
  struct ListenerWrapper;

  void RunInIOThread(ListenerWrapper* listener);

  // Should really be std::expected or std::experimental::fundamental_v3::expected
  // when upgrade the compiler.
  typedef std::tuple<ConnectionHandler*, ::boost::system::error_code>
    AcceptResult;

  AcceptResult AcceptConnection(ListenerWrapper* listener, ConnectionHandler::Notifier* done);

  IoContextPool* pool_;

  struct ListenerWrapper {
    ::boost::asio::ip::tcp::acceptor acceptor;
    ListenerInterface* listener;
    unsigned short port;

    ListenerWrapper(io_context* cntx, const endpoint& ep,
             ListenerInterface* si) : acceptor(*cntx, ep), listener(si) {
      port = acceptor.local_endpoint().port();
    }
  };

  ::boost::asio::signal_set signals_;
  fibers_ext::BlockingCounter bc_;

  bool was_run_ = false;

  std::vector<ListenerWrapper> listeners_;
};

}  // namespace util
