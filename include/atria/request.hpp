#pragma once

#include "atria/headers.hpp"
#include "atria/json.hpp"
#include "atria/method.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

enum class HttpVersion : std::uint8_t {
  Http10,
  Http11,
};

class Request {
public:
  using PathParams = std::vector<std::pair<std::string, std::string>>;
  using QueryParams = std::vector<std::pair<std::string, std::string>>;

  Request() = default;
  Request(
      Method method,
      std::string path,
      std::string query_raw,
      Headers headers,
      std::string body
  );

  [[nodiscard]] Method method() const noexcept { return method_; }

  [[nodiscard]] HttpVersion version() const noexcept { return version_; }

  [[nodiscard]] std::string_view path() const noexcept { return path_; }

  [[nodiscard]] std::string_view query_raw() const noexcept { return query_raw_; }

  [[nodiscard]] const Headers& headers() const noexcept { return headers_; }

  [[nodiscard]] std::string_view body() const noexcept { return body_; }

  [[nodiscard]] std::optional<std::string_view> header(std::string_view key) const;
  [[nodiscard]] std::optional<std::string_view> query(std::string_view key) const;
  [[nodiscard]] std::optional<std::string_view> path_param(std::string_view key) const;
  [[nodiscard]] std::expected<Json, JsonError> json(Json::ParseLimits limits = {}) const;
  [[nodiscard]] std::expected<Json, JsonError>
  json(JsonKeyStyle key_style, Json::ParseLimits limits = {}) const;

  void set_version(HttpVersion v) noexcept { version_ = v; }

  void set_path_params(PathParams params);
  void set_query_params(QueryParams params);

  [[nodiscard]] const PathParams& path_params() const noexcept { return path_params_; }

  [[nodiscard]] const QueryParams& query_params() const noexcept { return query_params_; }

private:
  Method method_{Method::Get};
  HttpVersion version_{HttpVersion::Http11};
  std::string path_;
  std::string query_raw_;
  Headers headers_;
  std::string body_;
  PathParams path_params_;
  QueryParams query_params_;
};

}  // namespace atria
