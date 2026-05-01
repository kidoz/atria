#pragma once

#include "atria/network_error.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace atria {

struct UdpEndpoint {
  std::string address;
  std::uint16_t port{0};
};

struct UdpReceiveResult {
  std::size_t bytes_received{0};
  UdpEndpoint remote;
};

class UdpSocket {
public:
  UdpSocket();
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;
  UdpSocket(UdpSocket&&) noexcept;
  UdpSocket& operator=(UdpSocket&&) noexcept;

  [[nodiscard]] static std::expected<UdpSocket, NetworkError> open_ipv4();
  [[nodiscard]] static std::expected<UdpSocket, NetworkError>
  bind_ipv4(std::string_view address, std::uint16_t port, bool reuse_address = true);

  [[nodiscard]] bool valid() const noexcept;
  void close() noexcept;

  [[nodiscard]] std::expected<std::uint16_t, NetworkError> local_port() const;

  [[nodiscard]] std::expected<void, NetworkError> set_receive_timeout(std::uint32_t milliseconds);

  [[nodiscard]] std::expected<std::size_t, NetworkError>
  send_to(std::string_view data, const UdpEndpoint& remote);

  [[nodiscard]] std::expected<UdpReceiveResult, NetworkError> receive_from(std::span<char> buffer);

  [[nodiscard]] std::expected<void, NetworkError> join_ipv4_multicast(
      std::string_view multicast_address,
      std::string_view interface_address = "0.0.0.0"
  );

  [[nodiscard]] std::expected<void, NetworkError> leave_ipv4_multicast(
      std::string_view multicast_address,
      std::string_view interface_address = "0.0.0.0"
  );

private:
  struct Impl;

  explicit UdpSocket(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace atria
