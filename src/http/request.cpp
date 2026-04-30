#include "atria/request.hpp"

#include "atria/headers.hpp"
#include "atria/method.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace atria {

Request::Request(
    Method method, std::string path, std::string query_raw, Headers headers, std::string body)
    : method_(method),
      path_(std::move(path)),
      query_raw_(std::move(query_raw)),
      headers_(std::move(headers)),
      body_(std::move(body)) {}

std::optional<std::string_view> Request::header(std::string_view key) const {
  return headers_.find(key);
}

std::optional<std::string_view> Request::query(std::string_view key) const {
  for (const auto& [k, v] : query_params_) {
    if (k == key) {
      return v;
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> Request::path_param(std::string_view key) const {
  for (const auto& [k, v] : path_params_) {
    if (k == key) {
      return v;
    }
  }
  return std::nullopt;
}

void Request::set_path_params(PathParams params) {
  path_params_ = std::move(params);
}

void Request::set_query_params(QueryParams params) {
  query_params_ = std::move(params);
}

}  // namespace atria
