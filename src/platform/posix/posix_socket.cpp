#include "platform/socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>

namespace atria::platform {

void global_init() {}

void global_shutdown() noexcept {}

void SocketHandle::close() noexcept {
  if (handle_ != kInvalidSocket) {
    ::close(handle_);
    handle_ = kInvalidSocket;
  }
}

namespace {

[[nodiscard]] SocketErrorKind classify(int err) noexcept {
  if (err == EAGAIN || err == EWOULDBLOCK) {
    return SocketErrorKind::WouldBlock;
  }
  if (err == ETIMEDOUT) {
    return SocketErrorKind::Timeout;
  }
  if (err == ECONNRESET || err == EPIPE) {
    return SocketErrorKind::ConnectionReset;
  }
  return SocketErrorKind::Other;
}

[[nodiscard]] SocketError errno_error(const char* what) {
  return SocketError{std::string{what} + ": " + std::strerror(errno), classify(errno)};
}

}  // namespace

std::expected<SocketHandle, SocketError>
listen_tcp(std::string_view host, std::uint16_t port, int backlog) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::unexpected(errno_error("socket"));
  }
  SocketHandle sock{fd};

  int yes = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    return std::unexpected(errno_error("setsockopt"));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  std::string host_str{host};
  if (host_str.empty() || host_str == "0.0.0.0") {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (host_str == "127.0.0.1" || host_str == "localhost") {
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else {
    if (::inet_pton(AF_INET, host_str.c_str(), &addr.sin_addr) != 1) {
      return std::unexpected(SocketError{"invalid host: " + host_str, SocketErrorKind::Other});
    }
  }

  if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    return std::unexpected(errno_error("bind"));
  }
  if (::listen(fd, backlog) < 0) {
    return std::unexpected(errno_error("listen"));
  }
  return sock;
}

std::expected<std::uint16_t, SocketError> local_port(SocketHandle& sock) {
  sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);
  if (::getsockname(sock.native(), reinterpret_cast<sockaddr*>(&addr), &addr_len) < 0) {
    return std::unexpected(errno_error("getsockname"));
  }
  return ntohs(addr.sin_port);
}

std::expected<SocketHandle, SocketError> connect_tcp(std::string_view host, std::uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::unexpected(errno_error("socket"));
  }
  SocketHandle sock{fd};

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  std::string host_str{host};
  if (host_str.empty() || host_str == "0.0.0.0" || host_str == "127.0.0.1" ||
      host_str == "localhost") {
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (::inet_pton(AF_INET, host_str.c_str(), &addr.sin_addr) != 1) {
    return std::unexpected(SocketError{"invalid host: " + host_str, SocketErrorKind::Other});
  }

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    return std::unexpected(errno_error("connect"));
  }
  return sock;
}

std::expected<SocketHandle, SocketError> accept_connection(SocketHandle& listener) {
  while (true) {
    int fd = ::accept(listener.native(), nullptr, nullptr);
    if (fd >= 0) {
      return SocketHandle{fd};
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected(errno_error("accept"));
  }
}

std::expected<SocketHandle, SocketError>
accept_connection_with_peer(SocketHandle& listener, std::string& peer_ip) {
  while (true) {
    sockaddr_storage peer_addr{};
    socklen_t peer_addr_len = sizeof(peer_addr);
    int accepted_fd =
        ::accept(listener.native(), reinterpret_cast<sockaddr*>(&peer_addr), &peer_addr_len);
    if (accepted_fd >= 0) {
      char address_text[INET6_ADDRSTRLEN] = {0};
      if (peer_addr.ss_family == AF_INET) {
        const auto* ipv4_addr = reinterpret_cast<const sockaddr_in*>(&peer_addr);
        ::inet_ntop(AF_INET, &ipv4_addr->sin_addr, address_text, sizeof(address_text));
      } else if (peer_addr.ss_family == AF_INET6) {
        const auto* ipv6_addr = reinterpret_cast<const sockaddr_in6*>(&peer_addr);
        ::inet_ntop(AF_INET6, &ipv6_addr->sin6_addr, address_text, sizeof(address_text));
      }
      peer_ip.assign(address_text);
      return SocketHandle{accepted_fd};
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected(errno_error("accept"));
  }
}

std::expected<std::size_t, SocketError> recv_some(SocketHandle& sock, char* buf, std::size_t cap) {
  while (true) {
    ssize_t n = ::recv(sock.native(), buf, cap, 0);
    if (n >= 0) {
      return static_cast<std::size_t>(n);
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected(errno_error("recv"));
  }
}

std::expected<std::size_t, SocketError> send_all(SocketHandle& sock, std::string_view data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
#if defined(MSG_NOSIGNAL)
    ssize_t n = ::send(sock.native(), data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
    ssize_t n = ::send(sock.native(), data.data() + sent, data.size() - sent, 0);
#endif
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(errno_error("send"));
    }
    sent += static_cast<std::size_t>(n);
  }
  return sent;
}

std::expected<std::size_t, SocketError> send_some(SocketHandle& sock, std::string_view data) {
  while (true) {
#if defined(MSG_NOSIGNAL)
    ssize_t n = ::send(sock.native(), data.data(), data.size(), MSG_NOSIGNAL);
#else
    ssize_t n = ::send(sock.native(), data.data(), data.size(), 0);
#endif
    if (n >= 0) {
      return static_cast<std::size_t>(n);
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected(errno_error("send_some"));
  }
}

namespace {

[[nodiscard]] std::expected<void, SocketError>
set_timeout(SocketHandle& sock, int optname, std::uint32_t milliseconds) {
  timeval tv{};
  tv.tv_sec = static_cast<decltype(tv.tv_sec)>(milliseconds / 1000U);
  tv.tv_usec = static_cast<decltype(tv.tv_usec)>((milliseconds % 1000U) * 1000U);
  if (::setsockopt(sock.native(), SOL_SOCKET, optname, &tv, sizeof(tv)) < 0) {
    return std::unexpected(errno_error("setsockopt(timeout)"));
  }
  return {};
}

}  // namespace

std::expected<void, SocketError> set_recv_timeout(SocketHandle& sock, std::uint32_t milliseconds) {
  return set_timeout(sock, SO_RCVTIMEO, milliseconds);
}

std::expected<void, SocketError> set_send_timeout(SocketHandle& sock, std::uint32_t milliseconds) {
  return set_timeout(sock, SO_SNDTIMEO, milliseconds);
}

std::expected<void, SocketError> set_nonblocking(SocketHandle& sock, bool enable) {
  int flags = ::fcntl(sock.native(), F_GETFL, 0);
  if (flags < 0) {
    return std::unexpected(errno_error("fcntl(F_GETFL)"));
  }
  int new_flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  if (::fcntl(sock.native(), F_SETFL, new_flags) < 0) {
    return std::unexpected(errno_error("fcntl(F_SETFL)"));
  }
  return {};
}

std::expected<SocketHandle, SocketError> udp_open_ipv4() {
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return std::unexpected(errno_error("socket(udp)"));
  }
  return SocketHandle{fd};
}

namespace {

[[nodiscard]] std::expected<in_addr, SocketError> parse_ipv4_address(std::string_view address) {
  in_addr parsed{};
  std::string address_text{address};
  if (address_text.empty() || address_text == "0.0.0.0") {
    parsed.s_addr = htonl(INADDR_ANY);
    return parsed;
  }
  if (address_text == "localhost") {
    parsed.s_addr = htonl(INADDR_LOOPBACK);
    return parsed;
  }
  if (::inet_pton(AF_INET, address_text.c_str(), &parsed) != 1) {
    return std::unexpected(
        SocketError{"invalid IPv4 address: " + address_text, SocketErrorKind::Other}
    );
  }
  return parsed;
}

[[nodiscard]] std::string ipv4_to_string(const in_addr& address) {
  char text[INET_ADDRSTRLEN] = {0};
  if (::inet_ntop(AF_INET, &address, text, sizeof(text)) == nullptr) {
    return {};
  }
  return text;
}

}  // namespace

std::expected<SocketHandle, SocketError>
udp_bind_ipv4(std::string_view address, std::uint16_t port, bool reuse_address) {
  auto socket = udp_open_ipv4();
  if (!socket.has_value()) {
    return std::unexpected(socket.error());
  }

  if (reuse_address) {
    int yes = 1;
    if (::setsockopt(socket->native(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
      return std::unexpected(errno_error("setsockopt(SO_REUSEADDR)"));
    }
  }

  auto parsed = parse_ipv4_address(address);
  if (!parsed.has_value()) {
    return std::unexpected(parsed.error());
  }

  sockaddr_in bind_address{};
  bind_address.sin_family = AF_INET;
  bind_address.sin_port = htons(port);
  bind_address.sin_addr = *parsed;
  if (::bind(
          socket->native(),
          reinterpret_cast<const sockaddr*>(&bind_address),
          sizeof(bind_address)
      ) < 0) {
    return std::unexpected(errno_error("bind(udp)"));
  }
  return socket;
}

std::expected<std::size_t, SocketError>
udp_send_to(SocketHandle& sock, std::string_view data, const UdpEndpoint& remote) {
  auto remote_address = parse_ipv4_address(remote.address);
  if (!remote_address.has_value()) {
    return std::unexpected(remote_address.error());
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(remote.port);
  address.sin_addr = *remote_address;
  while (true) {
    ssize_t sent = ::sendto(
        sock.native(),
        data.data(),
        data.size(),
        0,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    );
    if (sent >= 0) {
      return static_cast<std::size_t>(sent);
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected(errno_error("sendto"));
  }
}

std::expected<UdpReceiveResult, SocketError>
udp_recv_from(SocketHandle& sock, char* buffer, std::size_t capacity) {
  while (true) {
    sockaddr_in remote{};
    socklen_t remote_len = sizeof(remote);
    ssize_t received = ::recvfrom(
        sock.native(),
        buffer,
        capacity,
        0,
        reinterpret_cast<sockaddr*>(&remote),
        &remote_len
    );
    if (received >= 0) {
      return UdpReceiveResult{
          .bytes_received = static_cast<std::size_t>(received),
          .remote = UdpEndpoint{ipv4_to_string(remote.sin_addr), ntohs(remote.sin_port)},
      };
    }
    if (errno == EINTR) {
      continue;
    }
    return std::unexpected(errno_error("recvfrom"));
  }
}

std::expected<void, SocketError> udp_join_ipv4_multicast(
    SocketHandle& sock,
    std::string_view multicast_address,
    std::string_view interface_address
) {
  auto group = parse_ipv4_address(multicast_address);
  if (!group.has_value()) {
    return std::unexpected(group.error());
  }
  auto iface = parse_ipv4_address(interface_address);
  if (!iface.has_value()) {
    return std::unexpected(iface.error());
  }
  ip_mreq request{};
  request.imr_multiaddr = *group;
  request.imr_interface = *iface;
  if (::setsockopt(sock.native(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) < 0) {
    return std::unexpected(errno_error("setsockopt(IP_ADD_MEMBERSHIP)"));
  }
  return {};
}

std::expected<void, SocketError> udp_leave_ipv4_multicast(
    SocketHandle& sock,
    std::string_view multicast_address,
    std::string_view interface_address
) {
  auto group = parse_ipv4_address(multicast_address);
  if (!group.has_value()) {
    return std::unexpected(group.error());
  }
  auto iface = parse_ipv4_address(interface_address);
  if (!iface.has_value()) {
    return std::unexpected(iface.error());
  }
  ip_mreq request{};
  request.imr_multiaddr = *group;
  request.imr_interface = *iface;
  if (::setsockopt(sock.native(), IPPROTO_IP, IP_DROP_MEMBERSHIP, &request, sizeof(request)) < 0) {
    return std::unexpected(errno_error("setsockopt(IP_DROP_MEMBERSHIP)"));
  }
  return {};
}

std::expected<std::vector<NetworkInterface>, SocketError> enumerate_ipv4_interfaces() {
  ifaddrs* raw_interfaces = nullptr;
  if (::getifaddrs(&raw_interfaces) != 0) {
    return std::unexpected(errno_error("getifaddrs"));
  }

  std::vector<NetworkInterface> result;
  for (ifaddrs* item = raw_interfaces; item != nullptr; item = item->ifa_next) {
    if (item->ifa_addr == nullptr || item->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    const auto* address = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
    std::string netmask;
    if (item->ifa_netmask != nullptr) {
      const auto* mask = reinterpret_cast<const sockaddr_in*>(item->ifa_netmask);
      netmask = ipv4_to_string(mask->sin_addr);
    }
    const unsigned int flags = item->ifa_flags;
    result.push_back(
        NetworkInterface{
            .name = item->ifa_name == nullptr ? std::string{} : std::string{item->ifa_name},
            .ipv4_address = ipv4_to_string(address->sin_addr),
            .netmask = std::move(netmask),
            .is_up = (flags & IFF_UP) != 0U,
            .is_loopback = (flags & IFF_LOOPBACK) != 0U,
            .supports_multicast = (flags & IFF_MULTICAST) != 0U,
        }
    );
  }
  ::freeifaddrs(raw_interfaces);
  return result;
}

}  // namespace atria::platform
