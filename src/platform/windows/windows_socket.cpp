#include "platform/socket.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iphlpapi.h>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")

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

std::expected<SocketHandle, SocketError> udp_open_ipv4() {
  global_init();
  SOCKET fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == INVALID_SOCKET) {
    return std::unexpected(last_wsa_error("socket(udp)"));
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
  if (::InetPtonA(AF_INET, address_text.c_str(), &parsed) != 1) {
    return std::unexpected(
        SocketError{"invalid IPv4 address: " + address_text, SocketErrorKind::Other}
    );
  }
  return parsed;
}

[[nodiscard]] std::string ipv4_to_string(const in_addr& address) {
  char text[INET_ADDRSTRLEN] = {0};
  if (::InetNtopA(AF_INET, const_cast<in_addr*>(&address), text, sizeof(text)) == nullptr) {
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
    BOOL yes = 1;
    ::setsockopt(
        socket->native(),
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&yes),
        sizeof(yes)
    );
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
      ) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("bind(udp)"));
  }
  return socket;
}

std::expected<std::size_t, SocketError>
udp_send_to(SocketHandle& sock, std::string_view data, const UdpEndpoint& remote) {
  auto remote_address = parse_ipv4_address(remote.address);
  if (!remote_address.has_value()) {
    return std::unexpected(remote_address.error());
  }
  if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::unexpected(SocketError{"UDP datagram too large", SocketErrorKind::Other});
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(remote.port);
  address.sin_addr = *remote_address;
  int sent = ::sendto(
      sock.native(),
      data.data(),
      static_cast<int>(data.size()),
      0,
      reinterpret_cast<const sockaddr*>(&address),
      sizeof(address)
  );
  if (sent == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("sendto"));
  }
  return static_cast<std::size_t>(sent);
}

std::expected<UdpReceiveResult, SocketError>
udp_recv_from(SocketHandle& sock, char* buffer, std::size_t capacity) {
  int request = static_cast<int>(
      capacity > static_cast<std::size_t>(std::numeric_limits<int>::max())
          ? std::numeric_limits<int>::max()
          : capacity
  );
  sockaddr_in remote{};
  int remote_len = sizeof(remote);
  int received = ::recvfrom(
      sock.native(),
      buffer,
      request,
      0,
      reinterpret_cast<sockaddr*>(&remote),
      &remote_len
  );
  if (received == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("recvfrom"));
  }
  return UdpReceiveResult{
      .bytes_received = static_cast<std::size_t>(received),
      .remote = UdpEndpoint{ipv4_to_string(remote.sin_addr), ntohs(remote.sin_port)},
  };
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
  if (::setsockopt(
          sock.native(),
          IPPROTO_IP,
          IP_ADD_MEMBERSHIP,
          reinterpret_cast<const char*>(&request),
          sizeof(request)
      ) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("setsockopt(IP_ADD_MEMBERSHIP)"));
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
  if (::setsockopt(
          sock.native(),
          IPPROTO_IP,
          IP_DROP_MEMBERSHIP,
          reinterpret_cast<const char*>(&request),
          sizeof(request)
      ) == SOCKET_ERROR) {
    return std::unexpected(last_wsa_error("setsockopt(IP_DROP_MEMBERSHIP)"));
  }
  return {};
}

std::expected<std::vector<NetworkInterface>, SocketError> enumerate_ipv4_interfaces() {
  global_init();
  ULONG buffer_size = 0;
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST;
  ULONG first_result = ::GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &buffer_size);
  if (first_result != ERROR_BUFFER_OVERFLOW) {
    return std::unexpected(
        SocketError{"GetAdaptersAddresses sizing failed", SocketErrorKind::Other}
    );
  }

  std::vector<unsigned char> buffer(buffer_size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
  ULONG result = ::GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &buffer_size);
  if (result != NO_ERROR) {
    return std::unexpected(SocketError{"GetAdaptersAddresses failed", SocketErrorKind::Other});
  }

  std::vector<NetworkInterface> interfaces;
  for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
    for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
         unicast = unicast->Next) {
      if (unicast->Address.lpSockaddr == nullptr ||
          unicast->Address.lpSockaddr->sa_family != AF_INET) {
        continue;
      }
      const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
      std::uint32_t mask = 0;
      if (unicast->OnLinkPrefixLength >= 32U) {
        mask = 0xFFFFFFFFU;
      } else if (unicast->OnLinkPrefixLength > 0U) {
        mask = 0xFFFFFFFFU << (32U - unicast->OnLinkPrefixLength);
      }
      in_addr netmask{};
      netmask.s_addr = htonl(mask);
      interfaces.push_back(
          NetworkInterface{
              .name = adapter->AdapterName == nullptr ? std::string{}
                                                      : std::string{adapter->AdapterName},
              .ipv4_address = ipv4_to_string(address->sin_addr),
              .netmask = ipv4_to_string(netmask),
              .is_up = adapter->OperStatus == IfOperStatusUp,
              .is_loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK,
              .supports_multicast = (adapter->Flags & IP_ADAPTER_NO_MULTICAST) == 0U,
          }
      );
    }
  }
  return interfaces;
}

}  // namespace atria::platform
