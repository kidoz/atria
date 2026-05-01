#pragma once

#include "atria/status.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace atria {

struct ByteRange {
  std::uint64_t first{0};
  std::uint64_t last{0};

  [[nodiscard]] std::uint64_t size() const noexcept { return last - first + 1U; }
};

struct RangeError {
  Status status{Status::RangeNotSatisfiable};
  std::string message;
};

// Parses a single RFC 9110 byte range. Multi-range responses are intentionally not
// supported yet; DLNA/UPnP seek requests use a single range.
[[nodiscard]] std::expected<ByteRange, RangeError>
parse_byte_range(std::string_view header_value, std::uint64_t representation_size);

[[nodiscard]] std::string content_range_value(ByteRange range, std::uint64_t representation_size);
[[nodiscard]] std::string unsatisfied_content_range_value(std::uint64_t representation_size);

}  // namespace atria
