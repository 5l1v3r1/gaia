// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "base/ProducerConsumerQueue.h"

#include <boost/fiber/context.hpp>

#include "util/fibers/fibers_ext.h"

namespace util {
namespace fibers_ext {

/*
  This is single producer/single consumer thread-safe channel.
  It's fiber friendly, which means, multiple fibers at each end-point can use the channel:
  K fibers from producer thread can push and N fibers from consumer thread can pull the records.
  It has blocking interface that suspends blocked fibers upon empty/full conditions.
*/
template <typename T> class SimpleChannel {
  typedef ::boost::fibers::context::wait_queue_t wait_queue_t;
  using spinlock_lock_t = ::boost::fibers::detail::spinlock_lock;
  using mutex_t = ::boost::fibers::mutex;

 public:
  SimpleChannel(size_t n) : q_(n) {}

  template <typename... Args> void Push(Args&&... recordArgs);

  // Blocking call. Returns false if channel is closed, true otherwise with the popped value.
  bool Pop(T& dest);

  // Should be called only from the producer side. Signals the consumers that the channel
  // is going to be close. Consumers may still pop the existing items until Pop() return false.

  // This function does not block, only puts the channel into closing state.
  // It's responsibility of the caller to wait for the consumers to empty the remaining items
  // and stop using the channel.
  void StartClosing();

  // Non blocking
  template <typename... Args> bool TryPush(Args&&... args) {
    return q_.write(std::forward<Args>(args)...);
  }

  bool TryPop(T& val) { return q_.read(val); }

 private:
  folly::ProducerConsumerQueue<T> q_;
  std::atomic<int> pop_pending_{0};

  mutex_t mu_;
  condition_variable_any cnd_;
};

template <typename T> template <typename... Args> void SimpleChannel<T>::Push(Args&&... args) {
  if (TryPush(std::forward<Args>(args)...)) {  // fast path.
    // TODO: to notify blocked poppers.
    return;
  }
  std::unique_lock<mutex_t> lk(mu_);
  while (!TryPush(std::forward<Args>(args)...)) {
    cnd_.wait(lk);
  }
  /*bool wake = rue
  if (pop_waiting_) {
    cnd_.
  }*/
}

template <typename T> bool SimpleChannel<T>::Pop(T& dest) {
  if (TryPop(dest))
    return true;

}

}  // namespace fibers_ext
}  // namespace util
