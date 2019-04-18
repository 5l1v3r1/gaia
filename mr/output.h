// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include "base/type_traits.h"

#include "mr/mr3.pb.h"
#include "mr/mr_types.h"

namespace mr3 {

namespace detail {
  template <typename OutT> class TableImpl;
}

class OutputBase {
 public:
  pb::Output* mutable_msg() { return out_; }
  const pb::Output& msg() const { return *out_; }

 protected:
  pb::Output* out_;

  OutputBase(pb::Output* out) : out_(out) {}

  void SetCompress(pb::Output::CompressType ct, unsigned level);
  void SetShardSpec(pb::ShardSpec::Type st, unsigned modn = 0);
};

template <typename T> class Output : public OutputBase {
  friend class detail::TableImpl<T>;  // To allow the instantiation of Output<T>;

  // TODO: to make it variant.
  using CustomShardingFunc = std::function<std::string(const T&)>;
  using ModNShardingFunc = std::function<unsigned(const T&)>;

  absl::variant<ShardId, ModNShardingFunc, CustomShardingFunc> shard_op_;

 public:
  Output() : OutputBase(nullptr) {}

  template <typename U> Output& WithCustomSharding(U&& func) {
    static_assert(base::is_invocable_r<std::string, U, const T&>::value, "");
    shard_op_ = std::forward<U>(func);
    SetShardSpec(pb::ShardSpec::USER_DEFINED);

    return *this;
  }

  template <typename U> Output& WithModNSharding(unsigned modn, U&& func) {
    static_assert(base::is_invocable_r<unsigned, U, const T&>::value, "");
    shard_op_ = std::forward<U>(func);
    SetShardSpec(pb::ShardSpec::MODN, modn);

    return *this;
  }

  Output& AndCompress(pb::Output::CompressType ct, unsigned level = 0);

  ShardId Shard(const T& t) const {
    if (absl::holds_alternative<ModNShardingFunc>(shard_op_))
      return absl::get<ModNShardingFunc>(shard_op_)(t) %  out_->shard_spec().modn();

    if (absl::holds_alternative<CustomShardingFunc>(shard_op_))
      return absl::get<CustomShardingFunc>(shard_op_)(t);

    return absl::get<ShardId>(shard_op_);
  }

  void SetConstanShard(ShardId sid) {
    shard_op_ = sid;
  }
 private:
  Output(pb::Output* out) : OutputBase(out) {}
};

template <typename OutT>
Output<OutT>& Output<OutT>::AndCompress(pb::Output::CompressType ct, unsigned level) {
  SetCompress(ct, level);
  return *this;
}

}  // namespace mr3

namespace std {

template <> struct hash<mr3::ShardId> {
  size_t operator()(const mr3::ShardId& sid) const { return hash<mr3::ShardId::Parent>{}(sid); }
};

}  // namespace std
