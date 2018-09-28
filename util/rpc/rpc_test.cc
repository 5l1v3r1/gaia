// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include <chrono>
#include <memory>

#include "base/gtest.h"
#include "base/logging.h"
#include "base/walltime.h"

#if (__GNUC__ > 4)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "util/asio/asio_utils.h"
#include "util/asio/client_channel.h"
#include "util/asio/yield.h"
#include "util/rpc/frame_format.h"
#include "util/rpc/rpc_test_utils.h"

namespace util {
namespace rpc {

using namespace std;
using namespace boost;
using asio::ip::tcp;

class RpcTest : public ServerTest {

 protected:
  asio::mutable_buffer FrameBuffer() {
    size_t fr_sz = frame_.Write(buf_);
    return asio::mutable_buffer(buf_, fr_sz);
  }

  uint8_t buf_[Frame::kMaxByteSize];
  Frame frame_;
};

TEST_F(RpcTest, BadHeader) {
  // Must be large enough to pass the initial RPC server read.
  string control("Hello "), message("world!!!");
  ec_ = channel_->Write(make_buffer_seq(control, message));
  ASSERT_FALSE(ec_);

  ec_ = channel_->Read(do_not_lock, make_buffer_seq(control, message));
  ASSERT_EQ(asio::error::make_error_code(asio::error::eof), ec_) << ec_.message();
}

TEST_F(RpcTest, Basic) {
  // Must be large enough to pass the initial RPC server read.
  string header("Hello "), message("world!!!");
  frame_.header_size = header.size();
  frame_.letter_size = message.size();

  ec_ = channel_->Write(make_buffer_seq(FrameBuffer(), header, message));
  ASSERT_FALSE(ec_);

  ec_ = channel_->Apply(do_not_lock, [this](auto& s) {
    return frame_.Read(&s);
  });
  ASSERT_FALSE(ec_);
  EXPECT_EQ(frame_.header_size, header.size());
  EXPECT_EQ(frame_.letter_size, message.size());

  ec_ = channel_->Read(do_not_lock, make_buffer_seq(header, message));
  ASSERT_FALSE(ec_) << ec_.message();
}


TEST_F(RpcTest, Socket) {
  std::string send_msg(500, 'a');
  ec_ = channel_->Write(make_buffer_seq(send_msg));
  ASSERT_FALSE(ec_);

  ec_ = channel_->Apply(do_not_lock, [this] (tcp::socket& s) {
    return frame_.Read(&s);
  });
  ASSERT_TRUE(ec_) << ec_.message();

  frame_.header_size = send_msg.size();
  frame_.letter_size = 0;

  // Wait for reconnect.
  while (ec_) {
    SleepForMilliseconds(10);
    ec_ = channel_->Write(make_buffer_seq(FrameBuffer(), send_msg));
  }
  ec_ = channel_->Apply(do_not_lock, [this] (tcp::socket& s) {
    return frame_.Read(&s);
  });
  ASSERT_FALSE(ec_) << ec_.message();
}

TEST_F(RpcTest, SocketRead) {
  tcp::socket& sock = channel_->socket();
  ASSERT_TRUE(sock.non_blocking());
  ec_ = channel_->Write(make_buffer_seq(FrameBuffer()));
  ASSERT_FALSE(ec_);
  // std::vector<uint8_t> tmp(10000);
  //size_t sz = sock.read_some(asio::buffer(tmp));
  // LOG(INFO) << sz;
}


static void BM_ChannelConnection(benchmark::State& state) {
  IoContextPool pool(1);
  pool.Run();
  AcceptServer server(&pool);

  TestInterface ti;
  uint16_t port = ti.Listen(0, &server);

  server.Run();

  ClientChannel channel(pool.GetNextContext(), "127.0.0.1",
                        std::to_string(port));
  CHECK(!channel.Connect(500));
  Frame frame;
  std::string send_msg(100, 'a');
  uint8_t buf[Frame::kMaxByteSize];
  BufferType header, letter;
  frame.letter_size = send_msg.size();
  letter.resize(frame.letter_size);

  uint64_t fs = frame.Write(buf);
  auto& socket = channel.socket();
  BufferType tmp;
  tmp.resize(10000);

  auto buf_seq = make_buffer_seq(asio::buffer(buf, fs), send_msg);
  size_t total_sz = 0, buf_seq_sz = asio::buffer_size(buf_seq);

  while (state.KeepRunning()) {
    system::error_code ec = channel.Write(buf_seq);
    CHECK(!ec);
    // ec = frame.Read(&socket);
    total_sz += buf_seq_sz;

    DCHECK(!ec);
    DCHECK_EQ(0, frame.header_size);
    DCHECK_EQ(letter.size(), frame.letter_size);
    socket.read_some(asio::buffer(tmp), ec);
  }
  state.SetBytesProcessed(total_sz);
  server.Stop();
}
BENCHMARK(BM_ChannelConnection);

}  // namespace rpc
}  // namespace util
