// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include <string>

#include "mr/mr_types.h"
#include "mr/output.h"

namespace mr3 {

template <typename T> class DoContext;

namespace detail {
template <typename Handler, typename ToType> class HandlerWrapper;
}  // namespace detail

// User facing interfaces. void tag for dispatching per class of types
// (i.e. derived from protobuf::Message etc).
template <typename Record, typename = void> struct RecordTraits {
  static_assert(sizeof(base::void_t<Record>) == 0, "Please specify RecordTraits<> for this type");
};

template <> struct RecordTraits<std::string> {
  static std::string Serialize(bool is_binary, std::string&& r) { return std::move(r); }

  static bool Parse(bool is_binary, std::string&& tmp, std::string* res) {
    *res = std::move(tmp);
    return true;
  }
};

// This class is created per IO Context thread. In other words, RawContext is thread-local but
// not fiber local.
class RawContext {
  template<typename T> friend class DoContext;
 public:
  virtual ~RawContext();

  // Flushes pending written data before closing the context. Must be called before destroying
  // the context.
  virtual void Flush() {}
  virtual void CloseShard(const ShardId& sid) = 0;

  size_t parse_errors = 0;

 protected:
  virtual void WriteInternal(const ShardId& shard_id, std::string&& record) = 0;
};

// This class is created per MapFiber in SetupDoFn and it wraps RawContext.
template <typename T> class DoContext {
  template <typename Handler, typename ToType> friend class detail::HandlerWrapper;

 public:
  DoContext(const Output<T>& out, RawContext* context) : out_(out), context_(context) {}

  void Write(T&& t) {
    ShardId shard_id = out_.Shard(t);
    std::string dest = rt_.Serialize(out_.is_binary(), std::move(t));
    context_->WriteInternal(shard_id, std::move(dest));
  }

  RawContext* raw_context() { return context_; }

  void SetConstantShard(ShardId sid) { out_.SetConstantShard(sid); }

 private:
  Output<T> out_;
  RawContext* context_;
  RecordTraits<T> rt_;
};

}  // namespace mr3
