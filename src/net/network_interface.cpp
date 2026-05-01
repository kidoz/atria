#include "atria/network_interface.hpp"

#include "platform/socket.hpp"

#include <expected>
#include <optional>
#include <utility>
#include <vector>

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

}  // namespace

std::expected<std::vector<NetworkInterface>, NetworkError> enumerate_ipv4_interfaces() {
  auto interfaces = platform::enumerate_ipv4_interfaces();
  if (!interfaces.has_value()) {
    return std::unexpected(to_network_error(interfaces.error()));
  }

  std::vector<NetworkInterface> result;
  result.reserve(interfaces->size());
  for (auto& iface : *interfaces) {
    result.push_back(
        NetworkInterface{
            .name = std::move(iface.name),
            .ipv4_address = std::move(iface.ipv4_address),
            .netmask = std::move(iface.netmask),
            .is_up = iface.is_up,
            .is_loopback = iface.is_loopback,
            .supports_multicast = iface.supports_multicast,
        }
    );
  }
  return result;
}

std::expected<std::optional<NetworkInterface>, NetworkError> select_lan_ipv4_interface() {
  auto interfaces = enumerate_ipv4_interfaces();
  if (!interfaces.has_value()) {
    return std::unexpected(interfaces.error());
  }
  for (const auto& iface : *interfaces) {
    if (iface.is_up && !iface.is_loopback && iface.supports_multicast &&
        !iface.ipv4_address.empty()) {
      return iface;
    }
  }
  for (const auto& iface : *interfaces) {
    if (iface.is_up && !iface.is_loopback && !iface.ipv4_address.empty()) {
      return iface;
    }
  }
  return std::optional<NetworkInterface>{};
}

}  // namespace atria
