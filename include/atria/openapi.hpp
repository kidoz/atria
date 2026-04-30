#pragma once

#include "atria/json.hpp"

#include <cstdint>
#include <string>

namespace atria::openapi {

// Trait users specialize to provide an OpenAPI 3.x JSON schema for a request- or
// response-body type. The default returns a minimal "object" schema; specialize for your
// DTOs to get accurate documentation.
//
// Example:
//
//   struct CreateItemDto { std::string name; bool completed{}; };
//
//   template <>
//   struct atria::openapi::schema_for<CreateItemDto> {
//     static atria::Json get() {
//       return atria::Json::object({
//           {"type", "object"},
//           {"required", atria::Json::array({atria::Json{"name"}})},
//           {"properties", atria::Json::object({
//               {"name", atria::Json::object({{"type", "string"}})},
//               {"completed", atria::Json::object({{"type", "boolean"}})},
//           })},
//       });
//     }
//   };
template <typename T>
struct schema_for {
  static Json get() { return Json::object({{"type", "object"}}); }
};

// Built-in primitive specializations.
template <>
struct schema_for<bool> {
  static Json get() { return Json::object({{"type", "boolean"}}); }
};

template <>
struct schema_for<int> {
  static Json get() { return Json::object({{"type", "integer"}, {"format", "int32"}}); }
};

template <>
struct schema_for<std::int64_t> {
  static Json get() { return Json::object({{"type", "integer"}, {"format", "int64"}}); }
};

template <>
struct schema_for<double> {
  static Json get() { return Json::object({{"type", "number"}, {"format", "double"}}); }
};

template <>
struct schema_for<std::string> {
  static Json get() { return Json::object({{"type", "string"}}); }
};

}  // namespace atria::openapi
