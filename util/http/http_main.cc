// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "util/asio/accept_server.h"
#include "util/asio/io_context_pool.h"

#include "util/asio/yield.h"
#include "util/http/http_conn_handler.h"
#include "util/http/http_server.h"

#include "absl/strings/str_join.h"
#include "base/init.h"
#include "strings/stringpiece.h"

DEFINE_int32(port, 8080, "Port number.");
DEFINE_bool(new_server, true, "");

using namespace std;
using namespace boost;
using util::AcceptServer;
using util::IoContextPool;

int main(int argc, char** argv) {
  MainInitGuard guard(&argc, &argv);

  if (FLAGS_new_server) {
    IoContextPool pool;
    AcceptServer server(&pool);
    pool.Run();

    uint16_t port = server.AddListener(FLAGS_port, [] { return new util::http::HttpHandler(); });
    LOG(INFO) << "Listening on port " << port;

    server.Run();
    server.Wait();
  } else {
    http::Server server(FLAGS_port);
    util::Status status = server.Start();
    CHECK(status.ok()) << status;
    server.Wait();
  }

  LOG(INFO) << "Exiting server...";
  return 0;
}
