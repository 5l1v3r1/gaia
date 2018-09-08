// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include <boost/fiber/fiber.hpp>

#include "base/logging.h"
#include "util/asio/channel.h"


using namespace boost;
using asio::ip::tcp;
using namespace std;

namespace util {
using chrono::milliseconds;
namespace error = asio::error;

ClientChannel::Impl::~Impl() {
  resolver_.cancel();

  std::unique_lock<mutex> l(shd_mu_);

  VLOG(1) << "Before shd_cnd_.wait";
  shd_cnd_.wait(l, [this] { return !reconnect_active_;});
}

ClientChannel::~ClientChannel() {
  if (!impl_)
    return;
  impl_->shutting_down_ = true;
  sock_.close();
  impl_.reset();
}

system::error_code ClientChannel::Connect(uint32_t ms) {
  CHECK(!impl_->hostname_.empty());

  asio::steady_timer timer(sock_.get_executor().context(), milliseconds(ms));

  timer.async_wait([this](system::error_code timer_ec) {
      // Only if wait succeeded and connect  cb has not been run (ec is ok and is_connecte is false)
      // Only then cancel the socket.
      if (!timer_ec && status_ == asio::error::not_connected) {
        sock_.cancel();
        impl_->resolver_.cancel();
      }
    });
  ResolveAndConnect(timer.expiry());

  timer.cancel();

  return status_;
}

void ClientChannel::Shutdown() {
  if (!impl_)
    return;
  impl_->shutting_down_ = true;
  sock_.cancel();
}

using namespace std::chrono_literals;
using chrono::steady_clock;

void ClientChannel::ResolveAndConnect(const time_point& until) {
  auto sleep_dur = 100ms;
  VLOG(1) << "ClientChannel::ResolveAndConnect";

  while (!impl_->shutting_down_ && status_ && steady_clock::now() < until) {
    system::error_code resolve_ec;
    auto results = impl_->Resolve(fibers_ext::yield[resolve_ec]);
    if (!resolve_ec) {
      for (auto& ep : results) {
        sock_.async_connect(ep, fibers_ext::yield[status_]);
        if (!status_ || status_ == error::operation_aborted)
          return;
      }
    }
    time_point now = steady_clock::now();
    if (impl_->shutting_down_ || now + 2ms >= until) {
      status_ = error::operation_aborted;
      return;
    }

    time_point sleep_until = std::min(now + sleep_dur, until - 2ms);

    asio::steady_timer sleeper(sock_.get_executor().context(), sleep_until);
    sleeper.async_wait(fibers_ext::yield[resolve_ec]);
    if (sleep_dur < 1s)
      sleep_dur += 100ms;
  }
}

void ClientChannel::HandleErrorStatus() {
  CHECK(!impl_->reconnect_active_);

  std::lock_guard<mutex> guard(impl_->shd_mu_);
  if (impl_->shutting_down_) {
    return;
  }

  LOG(WARNING) << "Got " << status_.message() << ", reconnecting";
  impl_->reconnect_active_ = true;

  sock_.get_executor().context().post([this] {
    fibers::fiber(&ClientChannel::ReconnectFiber, this).detach();
  });
}

void ClientChannel::ReconnectFiber() {
  ResolveAndConnect(steady_clock::now() + 30s);

  bool shd = impl_->UpdateDisconnect();

  if (shd) {
    impl_->shd_cnd_.notify_one();
  } else {
    if (status_)
      HandleErrorStatus();
    else
      LOG(INFO) << "Reconnected";
  }
}

}  // namespace util
