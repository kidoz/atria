#include "net/tcp_listener.hpp"

#include "platform/socket.hpp"

#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

namespace atria::net {

std::expected<TcpListener, platform::SocketError> TcpListener::bind(std::string_view host,
                                                                     std::uint16_t port,
                                                                     int backlog) {
  auto sock = platform::listen_tcp(host, port, backlog);
  if (!sock.has_value()) {
    return std::unexpected(sock.error());
  }
  return TcpListener{std::move(*sock)};
}

}  // namespace atria::net
