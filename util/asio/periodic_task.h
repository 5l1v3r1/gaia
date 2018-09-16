// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once
#include <boost/asio/steady_timer.hpp>
#include <boost/fiber/condition_variable.hpp>

namespace util {

// Single threaded but fiber friendly PeriodicTask. Should run very short tasks which should
// not block the calling fiber.
// 'Cancel' blocks the calling fiber until the scheduled callback finished running.
class PeriodicTask {
  enum {ALARMED = 0x1, SHUTDOWN = 0x2};

 public:
  using timer_t = ::boost::asio::steady_timer;
  using duration_t = timer_t::duration;
  using error_code = boost::system::error_code;

  PeriodicTask(::boost::asio::io_context& cntx, duration_t d) : timer_(cntx), d_(d), state_(0) {}
  PeriodicTask(PeriodicTask&&) = default;

  ~PeriodicTask() { Cancel(); }

  // f must be non-blocking function because it runs directly in IO fiber.
  template<typename Func> void Start(Func&& f) {
    timer_.expires_after(d_);
    state_ |= ALARMED;

    RunInternal(std::forward<Func>(f));
  }

  // Cancels the task and blocks until all the callbacks finished to run.
  // Since it blocks - it should not run in IO fiber.
  void Cancel();

 private:
   template<typename Func> void RunInternal(Func&& f) {
    timer_.async_wait([this, f = std::move(f)] (const error_code& ec) mutable {
      if (ec == boost::asio::error::operation_aborted || (state_ & SHUTDOWN)) {
        Disalarm();
        return;
      }

      f();
      Start(std::move(f));
    });
  }

  void Disalarm();

  timer_t timer_;
  duration_t d_;
  uint8_t state_ ;
};

class PeriodicThreadTask {
 public:
  PeriodicThreadTask(::boost::asio::io_context& cntx, PeriodicTask::duration_t d) : pt_(cntx, d) {}

  void Start(std::function<void()> f) {
    pt_.Start([this, f = std::move(f)] () {
      StartThreaded(f);  // I do not move f, to allow calling this lambda many times.
    });
  }

  void Cancel();
 private:
  std::function<void()> WrappedFunc(std::function<void()> f);

  void StartThreaded(std::function<void()> f) {
    bool val = false;
    if (is_running_.compare_exchange_strong(val, true)) {
      std::thread t(WrappedFunc(std::move(f)));
      t.detach();
    };
  }

  std::atomic_bool is_running_{false};

  ::boost::fibers::mutex m_;
  ::boost::fibers::condition_variable   cond_;
  PeriodicTask pt_;
};

}  // namespace util

