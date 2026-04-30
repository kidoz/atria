#include "atria/validation.hpp"

#include "atria/json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace atria {

void ValidationError::add(std::string field, std::string message) {
  issues_.push_back({std::move(field), std::move(message)});
}

Validator& Validator::required(std::string_view field, const Json& json) {
  const Json* v = json.find(field);
  if (v == nullptr || v->is_null()) {
    error_.add(std::string{field}, "Field is required");
  }
  return *this;
}

Validator& Validator::string_required(std::string_view field, const Json& json) {
  const Json* v = json.find(field);
  if (v == nullptr || !v->is_string()) {
    error_.add(std::string{field}, "Must be a string");
  } else if (v->as_string().empty()) {
    error_.add(std::string{field}, "Must not be empty");
  }
  return *this;
}

Validator& Validator::string_min(std::string_view field, const Json& json, std::size_t min_chars) {
  const Json* v = json.find(field);
  if (v == nullptr || !v->is_string()) {
    return *this;
  }
  if (v->as_string().size() < min_chars) {
    error_.add(std::string{field}, "Must be at least " + std::to_string(min_chars) + " characters");
  }
  return *this;
}

Validator& Validator::string_max(std::string_view field, const Json& json, std::size_t max_chars) {
  const Json* v = json.find(field);
  if (v == nullptr || !v->is_string()) {
    return *this;
  }
  if (v->as_string().size() > max_chars) {
    error_.add(std::string{field}, "Must be at most " + std::to_string(max_chars) + " characters");
  }
  return *this;
}

Validator& Validator::boolean_optional(std::string_view field, const Json& json) {
  const Json* v = json.find(field);
  if (v == nullptr || v->is_null()) {
    return *this;
  }
  if (!v->is_bool()) {
    error_.add(std::string{field}, "Must be a boolean");
  }
  return *this;
}

Validator& Validator::integer_optional(std::string_view field, const Json& json) {
  const Json* v = json.find(field);
  if (v == nullptr || v->is_null()) {
    return *this;
  }
  if (!v->is_int()) {
    error_.add(std::string{field}, "Must be an integer");
  }
  return *this;
}

Json to_error_json(std::string_view code, std::string_view message,
                   const ValidationError& error) {
  Json::Array details;
  details.reserve(error.issues().size());
  for (const auto& issue : error.issues()) {
    details.push_back(Json::object({{"field", issue.field}, {"message", issue.message}}));
  }
  return Json::object({
      {"error", Json::object({
                    {"code", std::string{code}},
                    {"message", std::string{message}},
                    {"details", std::move(details)},
                })},
  });
}

}  // namespace atria
