#include "platform/socket.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

namespace atria::platform {

namespace {

std::mutex g_init_mutex;
bool g_initialized = false;

[[nodiscard]] SocketErrorKind classify(int err) noexcept {
  switch (err) {
  case WSAEWOULDBLOCK:
    return SocketErrorKind::WouldBlock;
  case WSAETIMEDOUT:
    return SocketErrorKind::Timeout;
  case WSAECONNRESET:
  case WSAECONNABORTED:
    return SocketErrorKind::ConnectionReset;
  default:
    return SocketErrorKind::Other;
  }
}

[[nodiscard]] SocketError last_wsa_error(const char* what) {
  int err = WSAGetLastError();
  return SocketError{std::string{what} + ": WSA error " + std::to_string(err), classify(err)};
}

}  // namespace

void global_init() {
  std::lock_guard<std::mutex> lock(g_init_mutex);
  if (g_initialized) {
    return;
  }
  WSADATA data{};
  WSAStartup(MAKEWORD(2, 2), &data);
  g_initialized = true;
}

void global_shutdown() noexcept {
  std::lock_guard<std::mutex> lock(g_init_mutex);
  if (g_initialized) {
    WSACleanup();
    g_initialized = false;
  }
}

void SocketHandle::close() noexcept {
  if (handle_ != kInvalidSocket) {
    ::closesocket(handle_);
    handle_ = kInvalidSocket;
  }
}

std::expected<SocketHandle, SocketError>
listen_tcp(std::string_view host, std::uint16_t port, int backlog) {
  global_init();
  SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == INVALID_SOCKET) {
    return std::unexpected(last_wsa_error("socket"));
  }
  SocketHandle sock{fd};

  BOOL yes = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  std::string host_str{host};
  if (host_str.empty() || host_str == "0.0.0.0") {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (host_str == "127.0.0.1" || host_str == "localhost") {
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else {
    if (::InetPtonA(AF_INET, host_str.c_str(), &addr.sin_addr) != 1) {
      return std::unexpected(SocketError{"invalid host: " + host_str, SocketErrorKind::Other});
    }
  }

  if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("bind"));
  }
  if (::listen(fd, backlog) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("listen"));
  }
  return sock;
}

std::expected<std::uint16_t, SocketError> local_port(SocketHandle& sock) {
  sockaddr_in addr{};
  int addr_len = sizeof(addr);
  if (::getsockname(sock.native(), reinterpret_cast<sockaddr*>(&addr), &addr_len) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("getsockname"));
  }
  return ntohs(addr.sin_port);
}

std::expected<SocketHandle, SocketError> connect_tcp(std::string_view host, std::uint16_t port) {
  global_init();
  SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == INVALID_SOCKET) {
    return std::unexpected(last_wsa_error("socket"));
  }
  SocketHandle sock{fd};

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  std::string host_str{host};
  if (host_str.empty() || host_str == "0.0.0.0" || host_str == "127.0.0.1" ||
      host_str == "localhost") {
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (::InetPtonA(AF_INET, host_str.c_str(), &addr.sin_addr) != 1) {
    return std::unexpected(SocketError{"invalid host: " + host_str, SocketErrorKind::Other});
  }

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("connect"));
  }
  return sock;
}

std::expected<SocketHandle, SocketError> accept_connection(SocketHandle& listener) {
  SOCKET fd = ::accept(listener.native(), nullptr, nullptr);
  if (fd == INVALID_SOCKET) {
    return std::unexpected(last_wsa_error("accept"));
  }
  return SocketHandle{fd};
}

std::expected<SocketHandle, SocketError>
accept_connection_with_peer(SocketHandle& listener, std::string& peer_ip) {
  sockaddr_storage peer_addr{};
  int peer_addr_len = sizeof(peer_addr);
  SOCKET accepted_socket =
      ::accept(listener.native(), reinterpret_cast<sockaddr*>(&peer_addr), &peer_addr_len);
  if (accepted_socket == INVALID_SOCKET) {
    return std::unexpected(last_wsa_error("accept"));
  }
  char address_text[INET6_ADDRSTRLEN] = {0};
  if (peer_addr.ss_family == AF_INET) {
    const auto* ipv4_addr = reinterpret_cast<const sockaddr_in*>(&peer_addr);
    ::InetNtopA(AF_INET, &ipv4_addr->sin_addr, address_text, sizeof(address_text));
  } else if (peer_addr.ss_family == AF_INET6) {
    const auto* ipv6_addr = reinterpret_cast<const sockaddr_in6*>(&peer_addr);
    ::InetNtopA(AF_INET6, &ipv6_addr->sin6_addr, address_text, sizeof(address_text));
  }
  peer_ip.assign(address_text);
  return SocketHandle{accepted_socket};
}

std::expected<std::size_t, SocketError> recv_some(SocketHandle& sock, char* buf, std::size_t cap) {
  int request = static_cast<int>(cap > 0x7FFFFFFF ? 0x7FFFFFFF : cap);
  int n = ::recv(sock.native(), buf, request, 0);
  if (n == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("recv"));
  }
  return static_cast<std::size_t>(n);
}

std::expected<std::size_t, SocketError> send_all(SocketHandle& sock, std::string_view data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    std::size_t remaining = data.size() - sent;
    int chunk = static_cast<int>(remaining > 0x7FFFFFFF ? 0x7FFFFFFF : remaining);
    int n = ::send(sock.native(), data.data() + sent, chunk, 0);
    if (n == SOCKET_ERROR) {
      return std::unexpected(last_wsa_error("send"));
    }
    sent += static_cast<std::size_t>(n);
  }
  return sent;
}

std::expected<std::size_t, SocketError> send_some(SocketHandle& sock, std::string_view data) {
  int chunk = static_cast<int>(data.size() > 0x7FFFFFFF ? 0x7FFFFFFF : data.size());
  int n = ::send(sock.native(), data.data(), chunk, 0);
  if (n == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("send_some"));
  }
  return static_cast<std::size_t>(n);
}

std::expected<void, SocketError> set_recv_timeout(SocketHandle& sock, std::uint32_t milliseconds) {
  DWORD ms = milliseconds;
  if (::setsockopt(
          sock.native(),
          SOL_SOCKET,
          SO_RCVTIMEO,
          reinterpret_cast<const char*>(&ms),
          sizeof(ms)
      ) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("setsockopt(SO_RCVTIMEO)"));
  }
  return {};
}

std::expected<void, SocketError> set_send_timeout(SocketHandle& sock, std::uint32_t milliseconds) {
  DWORD ms = milliseconds;
  if (::setsockopt(
          sock.native(),
          SOL_SOCKET,
          SO_SNDTIMEO,
          reinterpret_cast<const char*>(&ms),
          sizeof(ms)
      ) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("setsockopt(SO_SNDTIMEO)"));
  }
  return {};
}

std::expected<void, SocketError> set_nonblocking(SocketHandle& sock, bool enable) {
  u_long mode = enable ? 1 : 0;
  if (::ioctlsocket(sock.native(), FIONBIO, &mode) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("ioctlsocket(FIONBIO)"));
  }
  return {};
}

}  // namespace atria::platform
