#pragma once

#include <cstdint>
#include <string>

namespace atria {

enum class NetworkErrorKind : std::uint8_t {
  Other,
  WouldBlock,
  Timeout,
  ConnectionReset,
  Closed,
};

struct NetworkError {
  std::string message;
  NetworkErrorKind kind{NetworkErrorKind::Other};
};

}  // namespace atria
