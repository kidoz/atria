#pragma once

#include "atria/server_config.hpp"
#include "platform/socket.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>

namespace atria {
class Application;
}

namespace atria::net {

class ServerRuntime {
 public:
  ServerRuntime(Application& app, const ServerConfig& config);

  int run(std::atomic<bool>& running);

 private:
  Application& app_;
  ServerConfig config_;

  void handle_connection(platform::SocketHandle conn);
  std::string read_request_bytes(platform::SocketHandle& conn);
};

}  // namespace atria::net
