#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <iostream>
#include <memory>

#include "base/init.h"
#include "examples/pingserver/ping_command.h"

DEFINE_int32(http_port, 8080, "Http port.");
DEFINE_int32(port, 6380, "Redis port");
using namespace boost;
using asio::ip::tcp;
using std::string;

inline bool IsExpectedFinish(system::error_code ec) {
  using namespace boost::asio;
  return ec == error::eof || ec == error::operation_aborted || ec == error::connection_reset ||
         ec == error::not_connected;
}

class RedisConnection : public std::enable_shared_from_this<RedisConnection> {
 public:
  RedisConnection(tcp::socket socket) : socket_(std::move(socket)) {
    ConfigureSocket(&socket_);
  }

  void start() {
    do_read_cmd();
  }

 private:
  void do_read_cmd() {
    auto self(shared_from_this());

    auto cb = [this, self](boost::system::error_code ec, std::size_t length) {
      if (ec) {
        LOG_IF(ERROR, !IsExpectedFinish(ec)) << ec;
        return;
      }

      if (cmd_.Decode(length)) {
        do_reply();
      } else {
        do_read_cmd();
      }
    };

    socket_.async_read_some(cmd_.read_buffer(), cb);
  }

  void do_reply() {
    auto self = shared_from_this();
    auto cb = [this, self](system::error_code ec, std::size_t /*length*/) {
      if (!ec) {
        do_read_cmd();
      } else {
        LOG(ERROR) << ec;
      }
    };

    asio::async_write(socket_, cmd_.reply(), cb);
  }

  tcp::socket socket_;
  PingCommand cmd_;
};

class PingServer {
 public:
  PingServer(boost::asio::io_context& io_context, const tcp::endpoint& endpoint)
      : acceptor_(io_context, endpoint) {
    do_accept();
  }

 private:
  void do_accept() {
    auto cb = [this](boost::system::error_code ec, tcp::socket socket) {
      if (!ec) {
        std::make_shared<RedisConnection>(std::move(socket))->start();
      }

      do_accept();
    };

    acceptor_.async_accept(cb);
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
  MainInitGuard guard(&argc, &argv);
  try {
    tcp::endpoint endpoint(tcp::v4(), FLAGS_port);
    boost::asio::io_context io_context;

    PingServer srv(io_context, endpoint);

    io_context.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
