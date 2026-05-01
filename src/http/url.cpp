#include "atria/url.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

namespace {

[[nodiscard]] int from_hex(unsigned char c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

}  // namespace

std::expected<std::string, UrlError> percent_decode(std::string_view input, bool decode_plus) {
  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (c == '+' && decode_plus) {
      out.push_back(' ');
      continue;
    }
    if (c != '%') {
      out.push_back(c);
      continue;
    }
    if (i + 2 >= input.size()) {
      return std::unexpected(UrlError{"truncated percent escape"});
    }
    int hi = from_hex(static_cast<unsigned char>(input[i + 1]));
    int lo = from_hex(static_cast<unsigned char>(input[i + 2]));
    if (hi < 0 || lo < 0) {
      return std::unexpected(UrlError{"invalid percent escape"});
    }
    out.push_back(static_cast<char>(static_cast<unsigned char>((hi << 4) | lo)));
    i += 2;
  }
  return out;
}

namespace {

[[nodiscard]] bool contains_decoded_path_separator(std::string_view segment) noexcept {
  return segment.contains('/') || segment.contains('\\');
}

[[nodiscard]] std::vector<std::string_view> split_path(std::string_view path) {
  std::vector<std::string_view> segments;
  std::size_t cursor = 0;
  if (!path.empty() && path.front() == '/') {
    cursor = 1;
  }
  while (cursor < path.size()) {
    auto slash = path.find('/', cursor);
    if (slash == std::string_view::npos) {
      slash = path.size();
    }
    segments.push_back(path.substr(cursor, slash - cursor));
    cursor = slash + 1;
  }
  return segments;
}

}  // namespace

std::expected<std::vector<std::string>, UrlError> decode_path_segments(std::string_view path) {
  auto raw_segments = split_path(path);
  std::vector<std::string> segments;
  segments.reserve(raw_segments.size());

  for (const auto raw_segment : raw_segments) {
    if (raw_segment.empty()) {
      continue;
    }
    auto decoded_segment = percent_decode(raw_segment, false);
    if (!decoded_segment.has_value()) {
      return std::unexpected(decoded_segment.error());
    }
    if (contains_decoded_path_separator(*decoded_segment)) {
      return std::unexpected(UrlError{"decoded path segment contains a path separator"});
    }
    if (*decoded_segment == ".") {
      continue;
    }
    if (*decoded_segment == "..") {
      if (!segments.empty()) {
        segments.pop_back();
      }
      continue;
    }
    segments.push_back(std::move(*decoded_segment));
  }

  return segments;
}

std::expected<std::string, UrlError> normalize_path(std::string_view path) {
  auto segments = decode_path_segments(path);
  if (!segments.has_value()) {
    return std::unexpected(segments.error());
  }

  std::string normalized{"/"};
  for (std::size_t i = 0; i < segments->size(); ++i) {
    if (i > 0) {
      normalized.push_back('/');
    }
    normalized.append(segments->at(i));
  }
  return normalized;
}

std::vector<std::pair<std::string, std::string>> parse_query(std::string_view query) {
  std::vector<std::pair<std::string, std::string>> result;
  if (query.empty()) {
    return result;
  }
  std::size_t i = 0;
  while (i < query.size()) {
    std::size_t amp = query.find('&', i);
    if (amp == std::string_view::npos) {
      amp = query.size();
    }
    std::string_view pair = query.substr(i, amp - i);
    if (!pair.empty()) {
      std::size_t eq = pair.find('=');
      std::string_view raw_key = (eq == std::string_view::npos) ? pair : pair.substr(0, eq);
      std::string_view raw_val =
          (eq == std::string_view::npos) ? std::string_view{} : pair.substr(eq + 1);
      auto key = percent_decode(raw_key).value_or(std::string{raw_key});
      auto val = percent_decode(raw_val).value_or(std::string{raw_val});
      result.emplace_back(std::move(key), std::move(val));
    }
    i = amp + 1;
  }
  return result;
}

}  // namespace atria
