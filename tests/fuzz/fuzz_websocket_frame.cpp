#include "net/websocket_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::string_view input{reinterpret_cast<const char*>(data), size};
  auto parsed = atria::net::websocket::parse_frame(input, 4096);
  if (parsed.status == atria::net::websocket::ParseStatus::Frame) {
    (void)atria::net::websocket::is_valid_utf8(parsed.frame.payload);
  }
  return 0;
}
