// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include <google/protobuf/message.h>

#include "mr/do_context.h"

namespace mr3 {

struct PB_Serializer {
  using Message = google::protobuf::Message;

  static std::string To(bool is_binary, const Message* msg);

  // Need std::string on stack because of json2pb which requires mutable string for insitu parsing.
  static bool From(bool is_binary, std::string tmp, Message* res);
};

template <typename PB>
class RecordTraits<PB, std::enable_if_t<std::is_base_of<google::protobuf::Message, PB>::value>>
    : public PB_Serializer {
 public:
  static std::string Serialize(bool is_binary, const PB& doc) {
    return PB_Serializer::To(is_binary, &doc);
  }

  static bool Parse(bool is_binary, std::string tmp, PB* res) {
    return PB_Serializer::From(is_binary, std::move(tmp), res);
  }

  static std::string TypeName() {
    PB msg;
    return msg.GetTypeName();
  }
};
}  // namespace mr3
