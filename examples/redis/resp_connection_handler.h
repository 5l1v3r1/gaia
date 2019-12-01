// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "util/asio/connection_handler.h"

namespace redis {

class RespParser;
class Command;
/**
 * @brief Server side handler that talks RESP (REdis Serialization Protocol)
 *
 */
class RespConnectionHandler : public ::util::ConnectionHandler {
 public:
  RespConnectionHandler(const std::vector<Command>& commands, util::IoContext* context);

 protected:
  boost::system::error_code HandleRequest() final;

 private:
  enum class IoState : uint8_t { READ_EOL = 1, READ_N = 2, HANDLE_STRING = 3 };

  using ErrorState = absl::variant<boost::system::error_code, IoState>;

  ErrorState HandleNextString(absl::string_view blob, RespParser* parser);
  boost::system::error_code HandleCommand(absl::string_view cmd);

  uint32_t num_commands_ = 1;
  uint32_t bulk_size_ = 0;

  enum class ReqState : uint8_t { INIT = 1, COMMAND_FINISH = 2 };
  std::vector<ReqState> states_;
  std::string line_buffer_;
  ::boost::asio::mutable_buffer bulk_str_;
  const std::vector<Command>& commands_;
};

class RespListener : public ::util::ListenerInterface {
 public:
  using Args = std::vector<absl::string_view>;

  RespListener();
  ~RespListener();

  void Init();

  util::ConnectionHandler* NewConnection(util::IoContext& context) final;

 private:
  ::boost::system::error_code PrintCommands(const Args& args, util::FiberSyncSocket* s);
  ::boost::system::error_code Ping(const Args& args, util::FiberSyncSocket* s);


  std::vector<Command> commands_;
};

}  // namespace redis
