#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace atria::platform {

#if defined(_WIN32)
using NativeSocket = SOCKET;
inline constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
inline constexpr NativeSocket kInvalidSocket = -1;
#endif

enum class SocketErrorKind : std::uint8_t {
  Other,
  WouldBlock,  // non-blocking op would block (EAGAIN/EWOULDBLOCK or WSAEWOULDBLOCK)
  Timeout,     // SO_RCVTIMEO / SO_SNDTIMEO fired
  ConnectionReset,
  Closed,  // peer closed cleanly (EOF)
};

struct SocketError {
  std::string message;
  SocketErrorKind kind{SocketErrorKind::Other};
};

struct UdpEndpoint {
  std::string address;
  std::uint16_t port{0};
};

struct UdpReceiveResult {
  std::size_t bytes_received{0};
  UdpEndpoint remote;
};

struct NetworkInterface {
  std::string name;
  std::string ipv4_address;
  std::string netmask;
  bool is_up{false};
  bool is_loopback{false};
  bool supports_multicast{false};
};

void global_init();
void global_shutdown() noexcept;

class SocketHandle {
public:
  SocketHandle() noexcept = default;

  explicit SocketHandle(NativeSocket handle) noexcept : handle_(handle) {}

  ~SocketHandle() { close(); }

  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;

  SocketHandle(SocketHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = kInvalidSocket;
  }

  SocketHandle& operator=(SocketHandle&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.handle_;
      other.handle_ = kInvalidSocket;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return handle_ != kInvalidSocket; }

  [[nodiscard]] NativeSocket native() const noexcept { return handle_; }

  void close() noexcept;

private:
  NativeSocket handle_{kInvalidSocket};
};

[[nodiscard]] std::expected<SocketHandle, SocketError>
listen_tcp(std::string_view host, std::uint16_t port, int backlog);

[[nodiscard]] std::expected<std::uint16_t, SocketError> local_port(SocketHandle& sock);

[[nodiscard]] std::expected<SocketHandle, SocketError>
connect_tcp(std::string_view host, std::uint16_t port);

[[nodiscard]] std::expected<SocketHandle, SocketError> accept_connection(SocketHandle& listener);

// Variant of accept_connection that also fills `peer_ip` with the canonical address of
// the remote peer (e.g. "127.0.0.1" or "::1"). The runtime uses this to enforce per-IP
// connection limits.
[[nodiscard]] std::expected<SocketHandle, SocketError>
accept_connection_with_peer(SocketHandle& listener, std::string& peer_ip);

[[nodiscard]] std::expected<std::size_t, SocketError>
recv_some(SocketHandle& sock, char* buf, std::size_t cap);

[[nodiscard]] std::expected<std::size_t, SocketError>
send_all(SocketHandle& sock, std::string_view data);

// Non-blocking-aware single send. Returns bytes actually sent (possibly less than data.size()).
// On EAGAIN/EWOULDBLOCK returns unexpected with kind=WouldBlock — caller should wait for
// writable readiness and try again with the remaining bytes.
[[nodiscard]] std::expected<std::size_t, SocketError>
send_some(SocketHandle& sock, std::string_view data);

// I/O timeouts via SO_RCVTIMEO / SO_SNDTIMEO. A value of 0 disables the timeout.
[[nodiscard]] std::expected<void, SocketError>
set_recv_timeout(SocketHandle& sock, std::uint32_t milliseconds);

[[nodiscard]] std::expected<void, SocketError>
set_send_timeout(SocketHandle& sock, std::uint32_t milliseconds);

// Non-blocking flag. Used by the event-loop runtime; not used by thread-per-connection.
[[nodiscard]] std::expected<void, SocketError> set_nonblocking(SocketHandle& sock, bool enable);

[[nodiscard]] std::expected<SocketHandle, SocketError> udp_open_ipv4();

[[nodiscard]] std::expected<SocketHandle, SocketError>
udp_bind_ipv4(std::string_view address, std::uint16_t port, bool reuse_address);

[[nodiscard]] std::expected<std::size_t, SocketError>
udp_send_to(SocketHandle& sock, std::string_view data, const UdpEndpoint& remote);

[[nodiscard]] std::expected<UdpReceiveResult, SocketError>
udp_recv_from(SocketHandle& sock, char* buffer, std::size_t capacity);

[[nodiscard]] std::expected<void, SocketError> udp_join_ipv4_multicast(
    SocketHandle& sock,
    std::string_view multicast_address,
    std::string_view interface_address
);

[[nodiscard]] std::expected<void, SocketError> udp_leave_ipv4_multicast(
    SocketHandle& sock,
    std::string_view multicast_address,
    std::string_view interface_address
);

[[nodiscard]] std::expected<std::vector<NetworkInterface>, SocketError> enumerate_ipv4_interfaces();

}  // namespace atria::platform
