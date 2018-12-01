// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "base/init.h"
#include "base/logging.h"

#include "absl/strings/str_split.h"
#include "strings/stringpiece.h"
#include "util/asio/io_context_pool.h"
#include "util/http/http_client.h"

using namespace util;
using namespace std;

DEFINE_string(connect, "localhost:8080", "");

int main(int argc, char** argv) {
  MainInitGuard guard(&argc, &argv);

  IoContextPool pool;
  pool.Run();
  vector<StringPiece> parts = absl::StrSplit(FLAGS_connect, ":");
  CHECK_EQ(2, parts.size());

  http::Client client(pool.GetNextContext());
  auto ec = client.Connect(parts[0], parts[1]);
  CHECK(!ec) << ec.message();

  pool.Stop();

  return 0;
}
