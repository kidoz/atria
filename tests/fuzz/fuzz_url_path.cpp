#include "atria/url.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::string_view input{reinterpret_cast<const char*>(data), size};
  auto query_decoded = atria::percent_decode(input, true);
  auto path_decoded = atria::percent_decode(input, false);
  auto path_segments = atria::decode_path_segments(input);
  auto normalized_path = atria::normalize_path(input);
  (void)atria::parse_query(input);
  (void)query_decoded.has_value();
  (void)path_decoded.has_value();
  (void)path_segments.has_value();
  (void)normalized_path.has_value();
  return 0;
}
