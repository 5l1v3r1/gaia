// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include "base/type_traits.h"
#include "util/asio/io_context.h"
#include "util/asio/yield.h"

namespace util {

struct do_not_lock_t { explicit do_not_lock_t() = default; };

constexpr do_not_lock_t do_not_lock{};

namespace detail {
using namespace boost;
using asio::is_const_buffer_sequence;
using asio::is_mutable_buffer_sequence;
using asio::ip::tcp;

class ClientChannelImpl {
  IoContext& io_context_;
  tcp::resolver resolver_;

  template <typename T>
  using is_cbuf_t = typename std::enable_if<is_const_buffer_sequence<T>::value>::type;

  template <typename T>
  using is_mbuf_t = typename std::enable_if<is_mutable_buffer_sequence<T>::value>::type;

  using mutex = boost::fibers::mutex;

 public:
  using ResolveResults =
      decltype(resolver_.async_resolve(tcp::v4(), {}, {}, fibers_ext::yield_t{}));

  template <typename Func>
  using socket_callable_t =
      std::enable_if_t<base::is_invocable_r<system::error_code, Func, tcp::socket&>::value,
                       system::error_code>;

  template <typename Func>
  using ec_returnable_t =
      std::enable_if_t<base::is_invocable_r<system::error_code, Func>::value, system::error_code>;


  ClientChannelImpl(IoContext& cntx, const std::string& hname, const std::string& s)
      : io_context_(cntx), resolver_(cntx.get_context()),
        hostname_(hname),
        service_(s),
        sock_(cntx.get_context(), tcp::v4()),
        handle_(sock_.native_handle()) {
  }

  ~ClientChannelImpl();

  tcp::socket::native_handle_type handle() const {
    return handle_;
  }

  system::error_code status() const {
    return status_;
  }

  bool shutting_down() const {
    return shutting_down_;
  }

  system::error_code Connect(uint32_t ms);

  // Shuts down and waits synchronously until reconnect callback finishes.
  void Shutdown();

  system::error_code WaitForReadAvailable() {
    system::error_code ec;

    if (status_) {
      sock_.async_wait(tcp::socket::wait_read, fibers_ext::yield[ec]);
    }
    return ec;
  }

  template <typename BufferSequence>
  system::error_code Write(const BufferSequence& seq, is_cbuf_t<BufferSequence>* = 0) {
    std::lock_guard<mutex> guard(wmu_);
    return Write(do_not_lock, seq);
  }

  template <typename BufferSequence>
  system::error_code Write(do_not_lock_t, const BufferSequence& seq, is_cbuf_t<BufferSequence>* = 0) {
    if (status_)
      return status_;

    asio::async_write(sock_, seq, fibers_ext::yield[status_]);
    if (status_)
      HandleErrorStatus();
    return status_;
  }

  // 'f' must be callable on 'socket&'. Returns error_code, the function is guaranteed to call 'f'.
  template <typename Func>
  socket_callable_t<Func> Write(Func&& f) {
    std::lock_guard<mutex> guard(wmu_);
    status_ = f(sock_);
    if (status_)
      HandleErrorStatus();
    return status_;
  }

  template <typename BufferSequence>
  system::error_code Read(const BufferSequence& seq, is_mbuf_t<BufferSequence>* = 0) {
    std::lock_guard<mutex> guard(rmu_);
    return Read(do_not_lock, seq);
  }

  template <typename BufferSequence>
  system::error_code Read(do_not_lock_t, const BufferSequence& seq,
                          is_mbuf_t<BufferSequence>* = 0) {
    if (status_)
      return status_;

    asio::async_read(sock_, seq, fibers_ext::yield[status_]);
    if (status_)
      HandleErrorStatus();
    return status_;
  }

  template <typename Func> socket_callable_t<Func> Apply(Func&& f) {
    status_ = f(sock_);
    if (status_)
      HandleErrorStatus();
    return status_;
  }

  template <typename Func> ec_returnable_t<Func> Apply(Func&& f) {
    status_ = f();
    if (status_)
      HandleErrorStatus();
    return status_;
  }

  IoContext& context() {
    return io_context_;
  }

  tcp::socket& socket() {
    return sock_;
  }

 private:
  using time_point = std::chrono::steady_clock::time_point;

  using fiber_t = ::boost::fibers::fiber;

  ResolveResults Resolve(fibers_ext::yield_t&& token) {
    return resolver_.async_resolve(tcp::v4(), hostname_, service_, std::move(token));
  }

  void ReconnectAsync() {
    sock_.get_executor().context().post(
        [this] { fiber_t(&ClientChannelImpl::ReconnectFiber, this).detach(); });
  }

  void ResolveAndConnect(const time_point& until);
  void HandleErrorStatus();
  void ReconnectFiber();

  std::string hostname_, service_;
  tcp::socket sock_;
  system::error_code status_ = asio::error::not_connected;

  mutex wmu_, rmu_;
  mutex shd_mu_;
  bool shutting_down_{false}, reconnect_active_{false};

  ::boost::fibers::condition_variable shd_cnd_;
  tcp::socket::native_handle_type handle_;
};

}  // namespace detail

class ClientChannel {
 public:
  using error_code = boost::system::error_code;
  using socket_t = detail::tcp::socket;

  // since we allow moveable semantics we should support default c'tor as well.
  ClientChannel() {
  }

  // "service" - port to which to connect.
  ClientChannel(IoContext& cntx, const std::string& hostname, const std::string& service)
      : impl_(new detail::ClientChannelImpl(cntx, hostname, service)) {
  }

  ClientChannel(ClientChannel&&) noexcept = default;
  ~ClientChannel();

  ClientChannel& operator=(ClientChannel&&) noexcept = default;

  // Should be called at most once to trigger the connection process. Should not be called more
  // than once because ClientChannel handles reconnects by itself.
  error_code Connect(uint32_t ms) {
    return impl_->Connect(ms);
  }

  // impl_ might be null due to object move.
  // Blocks the calling fiber until impl_ is shut down.
  void Shutdown() {
    if (impl_)
      impl_->Shutdown();
  }

  error_code WaitForReadAvailable() {
    return impl_->WaitForReadAvailable();
  }

  // impl_ might be null due to object move.
  bool is_shut_down() const {
    return !impl_ || impl_->shutting_down();
  }

  template <typename Writeable>
  error_code Write(Writeable&& wr) {
    return impl_->Write(std::forward<Writeable>(wr));
  }

  template <typename Writeable>
  error_code Write(do_not_lock_t, Writeable&& wr) {
    return impl_->Write(do_not_lock, std::forward<Writeable>(wr));
  }


  template <typename BufferSequence>
  error_code Read(do_not_lock_t, const BufferSequence& bs) {
    return impl_->Read(do_not_lock, bs);
  }

  template <typename Func>
  error_code Apply(do_not_lock_t, Func&& f) {
    return impl_->Apply(std::forward<Func>(f));
  }

  error_code status() const {
    return impl_->status();
  }

  socket_t::native_handle_type handle() const {
    return impl_->handle();
  }

  IoContext& context() {
    return impl_->context();
  }

  socket_t& socket() {
    return impl_->socket();
  }

 private:
  // Factor out most fields into Impl struct to allow moveable semantics for the channel.
  std::unique_ptr<detail::ClientChannelImpl> impl_;
};

}  // namespace util
