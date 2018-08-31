// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "base/init.h"
#include "base/logging.h"
#include "strings/strcat.h"

#include <boost/asio.hpp>

#include "util/asio/io_context_pool.h"
#include "util/asio/yield.h"
#include "util/fibers_done.h"
#include "util/http/http_server.h"
#include "util/http/varz_stats.h"
#include "util/rpc/rpc_server.h"
#include "util/rpc/async_client.h"

using namespace boost;
using namespace std;
using namespace util;

DEFINE_int32(http_port, 8080, "Port number.");
DEFINE_string(connect, "", "");
DEFINE_int32(count, 10, "");
DEFINE_int32(num_connections, 1, "");
DEFINE_int32(io_threads, 0, "");

using asio::ip::tcp;
using util::IoContextPool;
using util::fibers_ext::yield;
using util::ConnectionServerNotifier;
using rpc::AsyncClient;

http::VarzQps qps("echo-qps");


class PingBridge final : public rpc::ConnectionBridge {
 public:
  // header and letter are input/output parameters.
  // HandleEnvelope reads first the input and if everything is parsed fine, it sends
  // back another header, letter pair.
  Status HandleEnvelope(uint64_t rpc_id, base::PODArray<uint8_t>* header,
                                base::PODArray<uint8_t>* letter) override {
    qps.Inc();
    return Status::OK;
  }
};

class PingInterface final : public rpc::ServiceInterface {
 public:
  rpc::ConnectionBridge* CreateConnectionBridge() override { return new PingBridge{}; }
};


/*****************************************************************************
*   fiber function per client
*****************************************************************************/
void RunClient(boost::asio::io_context& context,
            unsigned msg_count, util::fibers_ext::Done* done) {
  LOG(INFO) << ": echo-client started";
  constexpr unsigned max_length = 1024;
  ClientChannel channel(context, FLAGS_connect, "9999");
  system::error_code ec = channel.Connect(1000);
  CHECK(!ec) << ec;

  std::unique_ptr<AsyncClient> client(new AsyncClient(std::move(channel)));
  rpc::BufferType header, letter;
  letter.resize(64);

  const int count = 1;
  char msgbuf[64];

  char* start = reinterpret_cast<char*>(letter.data());
  for (unsigned msg = 0; msg < msg_count; ++msg) {
    char* next = StrAppend(start, letter.size(), {count, ".", msg});
    letter.resize(next - start);

    VLOG(1) << ": Sending: " << msgbuf;

    AsyncClient::future_code_t fec = client->SendEnvelope(&header, &letter);
    system::error_code ec = fec.get();

    if ( ec == asio::error::eof) {
      return; //connection closed cleanly by peer
    } else if (ec) {
      LOG(ERROR) << "Error: " << ec;
    }
  }

  client.reset();
  done->notify();
  LOG(INFO) << ": echo-client stopped";
}

void client_pool(IoContextPool* pool) {
  vector<util::fibers_ext::Done> done_arr(pool->size());
  {
    for (unsigned i = 0; i < pool->size(); ++i) {
      asio::io_context& cntx = pool->operator[](i);
      cntx.post([&cntx, done = &done_arr[i]] {
        fibers::fiber(RunClient, std::ref(cntx), FLAGS_count, done).detach();
      });
    }
    for (auto& f : done_arr)
      f.wait();
  }
  LOG(INFO) << "Pool ended";
}

int main(int argc, char **argv) {
  MainInitGuard guard(&argc, &argv);

  std::unique_ptr<http::Server> http_server;
  unsigned io_threads = FLAGS_io_threads;
  if (io_threads == 0)
    io_threads = thread::hardware_concurrency();
  LOG(INFO) << "Running with " << io_threads << " threads";

  IoContextPool pool(io_threads);
  pool.Run();

  if (FLAGS_connect.empty()) {
    http_server.reset(new http::Server(FLAGS_http_port));
    util::Status status = http_server->Start();
    CHECK(status.ok()) << status;

    PingInterface pi;
    rpc::Server server(9999);
    server.BindTo(&pi);
    server.Run(&pool);
    server.Wait();
  } else {
    client_pool(&pool);
  }

  pool.Stop();
  pool.Join();

  return 0;
}
