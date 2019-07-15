// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#pragma once

#include <functional>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

namespace mr3 {

template <typename T> class DoContext;

template <typename FromType, typename Class, typename O>
using EmitMemberFn = void (Class::*)(FromType, DoContext<O>*);

using RawRecord = ::std::string;

typedef std::function<void(RawRecord&& record)> RawSinkCb;

template <typename Handler, typename ToType>
using RawSinkMethodFactory = std::function<RawSinkCb(Handler* handler, DoContext<ToType>* context)>;

struct ShardId : public absl::variant<absl::monostate, uint32_t, std::string> {
  using Parent = absl::variant<absl::monostate, uint32_t, std::string>;

  using Parent::Parent;

  ShardId() = default;

  std::string ToString(absl::string_view basename) const;
};

}  // namespace mr3

std::ostream& operator<<(std::ostream& os, const mr3::ShardId& sid);

namespace std {

template <> struct hash<mr3::ShardId> {
  size_t operator()(const mr3::ShardId& sid) const { return hash<mr3::ShardId::Parent>{}(sid); }
};

}  // namespace std
