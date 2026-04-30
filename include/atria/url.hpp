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

[[nodiscard]] std::expected<std::string, UrlError> percent_decode(std::string_view input);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> parse_query(std::string_view query);

}  // namespace atria
