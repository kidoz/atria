#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

struct UrlError {
  std::string message;
};

[[nodiscard]] std::expected<std::string, UrlError>
percent_decode(std::string_view input, bool decode_plus = true);
[[nodiscard]] std::expected<std::vector<std::string>, UrlError>
decode_path_segments(std::string_view path);
[[nodiscard]] std::expected<std::string, UrlError> normalize_path(std::string_view path);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> parse_query(std::string_view query);

}  // namespace atria
