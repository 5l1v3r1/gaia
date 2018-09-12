// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "util/asio/accept_server.h"
#include "util/asio/io_context_pool.h"

#include "util/asio/yield.h"
#include "util/http/http_conn_handler.h"

#include "absl/strings/str_join.h"
#include "base/init.h"
#include "strings/stringpiece.h"

DEFINE_int32(port, 8080, "Port number.");

using namespace std;
using namespace boost;
using namespace util;

int main(int argc, char** argv) {
  MainInitGuard guard(&argc, &argv);

  IoContextPool pool;
  AcceptServer server(&pool);
  pool.Run();
  http::CallbackRegistry registry;
  auto cb = [](const http::QueryArgs& args, http::HttpHandler::Response* dest) {
    dest->body() = "Bar";
  };

  registry.RegisterCb("/foo", false, cb);
  uint16_t port = server.AddListener(FLAGS_port, [&registry] {
    return new http::HttpHandler(&registry);
  });
  LOG(INFO) << "Listening on port " << port;

  server.Run();
  server.Wait();

  LOG(INFO) << "Exiting server...";
  return 0;
}
