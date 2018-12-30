// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "util/sentry/sentry.h"

#include "base/logging.h"
#include "strings/strcat.h"
#include "util/http/http_testing.h"

DECLARE_string(sentry_dsn);

namespace util {

using namespace boost;
using namespace std;
using namespace chrono_literals;
namespace h2 = beast::http;

namespace {

}  // namespace

class SentryTest : public util::HttpBaseTest {
 protected:
  unsigned req_ = 0;
};


TEST_F(SentryTest, Basic) {
  listener_.RegisterCb("/api/id/store/", false,
      [this](const http::QueryArgs& args, http::HttpHandler::SendFunction* send) {
    this->req_++;
    http::StringResponse resp = http::MakeStringResponse(h2::status::ok);

    return send->Invoke(std::move(resp));
  });

  FLAGS_sentry_dsn = absl::StrCat("foo@localhost:", port_, "/id");
  EnableSentry(&pool_->GetNextContext());
  LOG(ERROR) << "Try";
  this_fiber::sleep_for(10ms);
  EXPECT_EQ(1, req_);
}


}  // namespace util
