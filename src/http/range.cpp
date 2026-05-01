#include "atria/range.hpp"

#include <charconv>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace atria {

namespace {

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] RangeError range_error(std::string message) {
  return RangeError{Status::RangeNotSatisfiable, std::move(message)};
}

[[nodiscard]] std::expected<std::uint64_t, RangeError> parse_uint64(std::string_view value) {
  if (value.empty()) {
    return std::unexpected(range_error("empty byte position"));
  }
  std::uint64_t parsed = 0;
  auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return std::unexpected(range_error("invalid byte position"));
  }
  return parsed;
}

}  // namespace

std::expected<ByteRange, RangeError>
parse_byte_range(std::string_view header_value, std::uint64_t representation_size) {
  std::string_view value = trim_ows(header_value);
  constexpr std::string_view prefix = "bytes=";
  if (!value.starts_with(prefix)) {
    return std::unexpected(range_error("unsupported range unit"));
  }
  value.remove_prefix(prefix.size());
  value = trim_ows(value);
  if (value.empty() || value.find(',') != std::string_view::npos) {
    return std::unexpected(range_error("only a single byte range is supported"));
  }
  if (representation_size == 0) {
    return std::unexpected(range_error("empty representation has no satisfiable range"));
  }

  const auto dash = value.find('-');
  if (dash == std::string_view::npos) {
    return std::unexpected(range_error("missing range separator"));
  }

  const std::string_view first_text = trim_ows(value.substr(0, dash));
  const std::string_view last_text = trim_ows(value.substr(dash + 1));

  if (first_text.empty()) {
    auto suffix_size = parse_uint64(last_text);
    if (!suffix_size.has_value()) {
      return std::unexpected(suffix_size.error());
    }
    if (*suffix_size == 0) {
      return std::unexpected(range_error("suffix range size must be greater than zero"));
    }
    const std::uint64_t count =
        *suffix_size > representation_size ? representation_size : *suffix_size;
    return ByteRange{representation_size - count, representation_size - 1U};
  }

  auto first = parse_uint64(first_text);
  if (!first.has_value()) {
    return std::unexpected(first.error());
  }
  if (*first >= representation_size) {
    return std::unexpected(range_error("range starts after representation end"));
  }

  std::uint64_t last = representation_size - 1U;
  if (!last_text.empty()) {
    auto parsed_last = parse_uint64(last_text);
    if (!parsed_last.has_value()) {
      return std::unexpected(parsed_last.error());
    }
    if (*parsed_last < *first) {
      return std::unexpected(range_error("range end precedes start"));
    }
    last = *parsed_last >= representation_size ? representation_size - 1U : *parsed_last;
  }

  return ByteRange{*first, last};
}

std::string content_range_value(ByteRange range, std::uint64_t representation_size) {
  return "bytes " + std::to_string(range.first) + "-" + std::to_string(range.last) + "/" +
         std::to_string(representation_size);
}

std::string unsatisfied_content_range_value(std::uint64_t representation_size) {
  return "bytes */" + std::to_string(representation_size);
}

}  // namespace atria
