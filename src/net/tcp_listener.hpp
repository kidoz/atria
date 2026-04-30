#pragma once

#include "platform/socket.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace atria::net {

class TcpListener {
 public:
  TcpListener() = default;

  [[nodiscard]] static std::expected<TcpListener, platform::SocketError> bind(
      std::string_view host, std::uint16_t port, int backlog);

  [[nodiscard]] platform::SocketHandle& handle() noexcept { return socket_; }

 private:
  explicit TcpListener(platform::SocketHandle s) : socket_(std::move(s)) {}

  platform::SocketHandle socket_;
};

}  // namespace atria::net
