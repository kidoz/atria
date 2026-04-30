#pragma once

#include "atria/json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

struct ValidationIssue {
  std::string field;
  std::string message;
};

class ValidationError {
 public:
  ValidationError() = default;
  explicit ValidationError(std::vector<ValidationIssue> issues) : issues_(std::move(issues)) {}

  void add(std::string field, std::string message);

  [[nodiscard]] bool empty() const noexcept { return issues_.empty(); }
  [[nodiscard]] const std::vector<ValidationIssue>& issues() const noexcept { return issues_; }

 private:
  std::vector<ValidationIssue> issues_;
};

class Validator {
 public:
  Validator& required(std::string_view field, const Json& json);
  Validator& string_min(std::string_view field, const Json& json, std::size_t min_chars);
  Validator& string_max(std::string_view field, const Json& json, std::size_t max_chars);
  Validator& string_required(std::string_view field, const Json& json);
  Validator& boolean_optional(std::string_view field, const Json& json);
  Validator& integer_optional(std::string_view field, const Json& json);

  [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
  [[nodiscard]] const ValidationError& error() const noexcept { return error_; }
  [[nodiscard]] const std::vector<ValidationIssue>& errors() const noexcept {
    return error_.issues();
  }

 private:
  ValidationError error_;
};

[[nodiscard]] Json to_error_json(std::string_view code, std::string_view message,
                                  const ValidationError& error);

}  // namespace atria
