#pragma once

#include "atria/network_error.hpp"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace atria {

struct NetworkInterface {
  std::string name;
  std::string ipv4_address;
  std::string netmask;
  bool is_up{false};
  bool is_loopback{false};
  bool supports_multicast{false};
};

[[nodiscard]] std::expected<std::vector<NetworkInterface>, NetworkError>
enumerate_ipv4_interfaces();

[[nodiscard]] std::expected<std::optional<NetworkInterface>, NetworkError>
select_lan_ipv4_interface();

}  // namespace atria
