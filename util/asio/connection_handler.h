// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/fiber/condition_variable.hpp>
#include <boost/intrusive/list.hpp>
#include <experimental/optional>

namespace util {

class ConnectionHandler;

namespace detail {
using namespace ::boost::intrusive;

// auto_unlink allows unlinking from the container during the destruction of
// of hook without holding the reference to the container itself.
// Requires that the container won't have O(1) size function.
typedef slist_member_hook<link_mode<safe_link>> connection_hook;

}  // namespace detail


// An instance of this class handles a single connection in fiber.
class ConnectionHandler {
 public:
  using ptr_t = ::boost::intrusive_ptr<ConnectionHandler>;
  using io_context = ::boost::asio::io_context;
  using socket_t = ::boost::asio::ip::tcp::socket;
  using connection_hook_t = detail::connection_hook;

  connection_hook_t hook_;

  using member_hook_t = detail::member_hook<ConnectionHandler, detail::connection_hook,
                                            &ConnectionHandler::hook_> ;

  using ListType =
      detail::slist<ConnectionHandler, ConnectionHandler::member_hook_t,
                    detail::constant_time_size<true>, detail::cache_last<true>>;
  class Notifier;

  explicit ConnectionHandler() noexcept;

  virtual ~ConnectionHandler();

  // Can be trigerred from any thread. Schedules RunInIOThread to run in io_context loop.
  void Run();

  void Close();

  socket_t& socket() { return *socket_;}


  friend void intrusive_ptr_add_ref(ConnectionHandler* ctx) noexcept {
    ctx->use_count_.fetch_add(1, std::memory_order_relaxed);
  }

  friend void intrusive_ptr_release(ConnectionHandler* ctx) noexcept {
    if (1 == ctx->use_count_.fetch_sub(1, std::memory_order_release) ) {
      std::atomic_thread_fence( std::memory_order_acquire);
      delete ctx;
    }
  }

  // Fiber-safe wrapper around the list.
  // Allows locking on the list or wait till it becomes empty.
  class Notifier {
    boost::fibers::condition_variable cnd_;
    boost::fibers::mutex mu_;
    ListType* list_;
   public:
    explicit Notifier(ListType* list) : list_(list) {}

    void Unlink(ConnectionHandler* me) noexcept;

    std::unique_lock<boost::fibers::mutex> Lock() noexcept {
      return std::unique_lock<boost::fibers::mutex>(mu_);
    }

    void WaitTillEmpty(std::unique_lock<boost::fibers::mutex>& lock) noexcept {
      cnd_.wait(lock, [this] { return list_->empty(); });
    }
  };

  void Init(socket_t&& sock, Notifier* notifier) {
    socket_.emplace(std::move(sock));
    notifier_ = notifier;
  }

 protected:
  // Should not block the thread. Can fiber-block (fiber friendly).
  virtual boost::system::error_code HandleRequest() = 0;

  std::experimental::optional<socket_t> socket_;

 private:
  void RunInIOThread();

  Notifier* notifier_ = nullptr;
  std::atomic<std::uint32_t>  use_count_{0};
};

}  // namespace util
