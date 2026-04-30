#include "platform/socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fcntl.h>
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

}  // namespace atria::platform
