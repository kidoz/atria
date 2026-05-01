#include "atria/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::string_view input{reinterpret_cast<const char*>(data), size};
  atria::ParseLimits limits{
      .max_request_line_bytes = 1024,
      .max_header_bytes = 4096,
      .max_header_count = 64,
      .max_body_bytes = 4096,
  };
  std::size_t consumed = 0;
  auto parsed = atria::parse_request(input, limits, &consumed);
  if (parsed.has_value()) {
    (void)parsed->method();
    (void)parsed->path();
    (void)parsed->query_raw();
    (void)parsed->body();
  }
  return 0;
}
