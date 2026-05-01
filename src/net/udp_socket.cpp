#include "atria/udp.hpp"
#include "platform/socket.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace atria {

namespace {

[[nodiscard]] NetworkError to_network_error(const platform::SocketError& error) {
  NetworkErrorKind kind = NetworkErrorKind::Other;
  switch (error.kind) {
  case platform::SocketErrorKind::Other:
    kind = NetworkErrorKind::Other;
    break;
  case platform::SocketErrorKind::WouldBlock:
    kind = NetworkErrorKind::WouldBlock;
    break;
  case platform::SocketErrorKind::Timeout:
    kind = NetworkErrorKind::Timeout;
    break;
  case platform::SocketErrorKind::ConnectionReset:
    kind = NetworkErrorKind::ConnectionReset;
    break;
  case platform::SocketErrorKind::Closed:
    kind = NetworkErrorKind::Closed;
    break;
  }
  return NetworkError{error.message, kind};
}

[[nodiscard]] platform::UdpEndpoint to_platform_endpoint(const UdpEndpoint& endpoint) {
  return platform::UdpEndpoint{endpoint.address, endpoint.port};
}

}  // namespace

struct UdpSocket::Impl {
  platform::SocketHandle socket;
};

UdpSocket::UdpSocket() = default;
UdpSocket::~UdpSocket() = default;
UdpSocket::UdpSocket(UdpSocket&&) noexcept = default;
UdpSocket& UdpSocket::operator=(UdpSocket&&) noexcept = default;

UdpSocket::UdpSocket(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::expected<UdpSocket, NetworkError> UdpSocket::open_ipv4() {
  auto socket = platform::udp_open_ipv4();
  if (!socket.has_value()) {
    return std::unexpected(to_network_error(socket.error()));
  }
  auto impl = std::make_unique<Impl>();
  impl->socket = std::move(*socket);
  return UdpSocket{std::move(impl)};
}

std::expected<UdpSocket, NetworkError>
UdpSocket::bind_ipv4(std::string_view address, std::uint16_t port, bool reuse_address) {
  auto socket = platform::udp_bind_ipv4(address, port, reuse_address);
  if (!socket.has_value()) {
    return std::unexpected(to_network_error(socket.error()));
  }
  auto impl = std::make_unique<Impl>();
  impl->socket = std::move(*socket);
  return UdpSocket{std::move(impl)};
}

bool UdpSocket::valid() const noexcept {
  return impl_ != nullptr && impl_->socket.valid();
}

void UdpSocket::close() noexcept {
  if (impl_ != nullptr) {
    impl_->socket.close();
  }
}

std::expected<std::uint16_t, NetworkError> UdpSocket::local_port() const {
  if (!valid()) {
    return std::unexpected(NetworkError{"invalid UDP socket", NetworkErrorKind::Closed});
  }
  auto port = platform::local_port(impl_->socket);
  if (!port.has_value()) {
    return std::unexpected(to_network_error(port.error()));
  }
  return *port;
}

std::expected<void, NetworkError> UdpSocket::set_receive_timeout(std::uint32_t milliseconds) {
  if (!valid()) {
    return std::unexpected(NetworkError{"invalid UDP socket", NetworkErrorKind::Closed});
  }
  auto result = platform::set_recv_timeout(impl_->socket, milliseconds);
  if (!result.has_value()) {
    return std::unexpected(to_network_error(result.error()));
  }
  return {};
}

std::expected<std::size_t, NetworkError>
UdpSocket::send_to(std::string_view data, const UdpEndpoint& remote) {
  if (!valid()) {
    return std::unexpected(NetworkError{"invalid UDP socket", NetworkErrorKind::Closed});
  }
  auto sent = platform::udp_send_to(impl_->socket, data, to_platform_endpoint(remote));
  if (!sent.has_value()) {
    return std::unexpected(to_network_error(sent.error()));
  }
  return *sent;
}

std::expected<UdpReceiveResult, NetworkError> UdpSocket::receive_from(std::span<char> buffer) {
  if (!valid()) {
    return std::unexpected(NetworkError{"invalid UDP socket", NetworkErrorKind::Closed});
  }
  auto result = platform::udp_recv_from(impl_->socket, buffer.data(), buffer.size());
  if (!result.has_value()) {
    return std::unexpected(to_network_error(result.error()));
  }
  return UdpReceiveResult{
      .bytes_received = result->bytes_received,
      .remote = UdpEndpoint{result->remote.address, result->remote.port},
  };
}

std::expected<void, NetworkError> UdpSocket::join_ipv4_multicast(
    std::string_view multicast_address,
    std::string_view interface_address
) {
  if (!valid()) {
    return std::unexpected(NetworkError{"invalid UDP socket", NetworkErrorKind::Closed});
  }
  auto result =
      platform::udp_join_ipv4_multicast(impl_->socket, multicast_address, interface_address);
  if (!result.has_value()) {
    return std::unexpected(to_network_error(result.error()));
  }
  return {};
}

std::expected<void, NetworkError> UdpSocket::leave_ipv4_multicast(
    std::string_view multicast_address,
    std::string_view interface_address
) {
  if (!valid()) {
    return std::unexpected(NetworkError{"invalid UDP socket", NetworkErrorKind::Closed});
  }
  auto result =
      platform::udp_leave_ipv4_multicast(impl_->socket, multicast_address, interface_address);
  if (!result.has_value()) {
    return std::unexpected(to_network_error(result.error()));
  }
  return {};
}

}  // namespace atria
