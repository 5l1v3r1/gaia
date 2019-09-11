// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "util/http/https_client_pool.h"

#include "base/logging.h"
#include "util/http/https_client.h"

namespace util {

namespace http {

class HttpsClientPool::HandleGuard {
 public:
  HandleGuard(HttpsClientPool* pool) : pool_(pool) {}

  void operator()(HttpsClient* client) {
    CHECK(client);

    if (client->status()) {
      delete client;
    } else {
      pool_->available_handles_.emplace_back(client);
    }
  }

 private:
  HttpsClientPool* pool_;
};  // namespace http

HttpsClientPool::HttpsClientPool(const std::string& domain, ::boost::asio::ssl::context* ssl_ctx,
                                 IoContext* io_cntx)
    : ssl_cntx_(*ssl_ctx), io_cntx_(*io_cntx), domain_(domain) {}

HttpsClientPool::~HttpsClientPool() {}

auto HttpsClientPool::GetHandle() -> ClientHandle {
  while (!available_handles_.empty()) {
    auto ptr = std::move(available_handles_.back());
    available_handles_.pop_back();

    if (ptr->status()) {
      continue;  // we just throw a connection with error status.
    }

    // pass it further with custom deleter.
    return ClientHandle(ptr.release(), HandleGuard{this});
  }

  // available_handles_ are empty - create a new connection.
  std::unique_ptr<HttpsClient> client(new HttpsClient{domain_, &io_cntx_, &ssl_cntx_});

  auto ec = client->Connect(connect_msec_);

  LOG_IF(WARNING, ec) << "HttpsClientPool: Could not connect " << ec;

  return ClientHandle{client.release(), HandleGuard{this}};
}

}  // namespace http
}  // namespace util
