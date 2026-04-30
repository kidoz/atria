#pragma once

#include "atria/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace atria::openapi {

// Trait users specialize to provide an OpenAPI 3.x JSON schema for a request- or
// response-body type. The default returns a minimal "object" schema. Two ways to give a
// real schema:
//
//   (1) Use schema_builder<T> (recommended) — pointer-to-member-driven, types derived,
//       only field names are written by hand:
//
//         struct CreateItemDto { std::string name; bool completed{}; };
//
//         template <>
//         struct atria::openapi::schema_for<CreateItemDto> {
//           static atria::Json get() {
//             return atria::openapi::schema_builder<CreateItemDto>{}
//                 .field<&CreateItemDto::name>("name", {.required = true})
//                 .field<&CreateItemDto::completed>("completed")
//                 .build();
//           }
//         };
//
//   (2) Hand-roll the JSON Schema object (only when you need OpenAPI features the
//       builder doesn't model — enums, oneOf, references, etc.).
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

// Optional fields are unwrapped: schema_for<std::optional<T>> produces the same schema
// as schema_for<T> (presence is conveyed via the parent object's `required` array).
template <typename T>
struct schema_for<std::optional<T>> {
  static Json get() { return schema_for<T>::get(); }
};

// Per-field options for schema_builder. Designed to grow (description, default, example,
// enum, min/max). Today: required + description.
struct field_options {
  bool required{false};
  std::optional<std::string> description{};
};

// Fluent builder for object schemas. Each `field<&T::member>("name")` call uses the
// pointer-to-member to deduce the field's C++ type, then asks `schema_for<FieldType>`
// for its JSON Schema. Result: users only spell out names + required flags, never
// hand-write `{"type": ...}` blocks for primitive fields.
template <typename T>
class schema_builder {
 public:
  schema_builder() = default;

  template <auto Member>
  schema_builder& field(std::string name, field_options options = {}) {
    using ClassType = decltype(member_class(Member));
    static_assert(std::is_same_v<ClassType, T>,
                  "schema_builder<T>::field requires a pointer-to-member of T");
    using FieldType = decltype(member_type(Member));
    Json field_schema = schema_for<std::remove_cvref_t<FieldType>>::get();
    if (options.description.has_value()) {
      append_or_replace(field_schema, "description", Json{*options.description});
    }
    properties_.emplace_back(std::move(name), std::move(field_schema));
    if (options.required) {
      required_.push_back(properties_.back().first);
    }
    return *this;
  }

  // Override the JSON Schema for an already-listed field. Useful for enums or other
  // shapes the trait can't infer from the type alone.
  schema_builder& override_property(const std::string& name, Json schema) {
    for (auto& property : properties_) {
      if (property.first == name) {
        property.second = std::move(schema);
        return *this;
      }
    }
    properties_.emplace_back(name, std::move(schema));
    return *this;
  }

  schema_builder& description(std::string text) {
    description_ = std::move(text);
    return *this;
  }

  // Mark additional fields required after the fact (e.g. a field whose schema is set via
  // override_property but wasn't passed to field()).
  schema_builder& mark_required(std::string name) {
    required_.push_back(std::move(name));
    return *this;
  }

  [[nodiscard]] Json build() const {
    Json::Object root;
    root.emplace_back("type", std::string{"object"});
    if (description_.has_value()) {
      root.emplace_back("description", *description_);
    }
    if (!properties_.empty()) {
      Json::Object properties;
      properties.reserve(properties_.size());
      for (const auto& [field_name, field_schema] : properties_) {
        properties.emplace_back(field_name, field_schema);
      }
      root.emplace_back("properties", Json{std::move(properties)});
    }
    if (!required_.empty()) {
      Json::Array required_list;
      required_list.reserve(required_.size());
      for (const auto& name : required_) {
        required_list.emplace_back(name);
      }
      root.emplace_back("required", Json{std::move(required_list)});
    }
    return Json{std::move(root)};
  }

 private:
  // Pointer-to-member type extraction. Both helpers are unevaluated; only their return
  // types are used inside decltype(). Declaration-only so they don't require ClassType
  // or FieldType to be default-constructible.
  template <typename ClassType, typename FieldType>
  static ClassType member_class(FieldType ClassType::*);
  template <typename ClassType, typename FieldType>
  static FieldType member_type(FieldType ClassType::*);

  static void append_or_replace(Json& target, std::string_view key, Json value) {
    if (!target.is_object()) {
      target = Json::object({{std::string{key}, std::move(value)}});
      return;
    }
    // Json's object is order-preserving; we either replace the existing entry or append.
    auto target_object = target.as_object();
    bool replaced = false;
    for (auto& [existing_key, existing_value] : target_object) {
      if (existing_key == key) {
        existing_value = std::move(value);
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      target_object.emplace_back(std::string{key}, std::move(value));
    }
    target = Json{std::move(target_object)};
  }

  std::vector<std::pair<std::string, Json>> properties_;
  std::vector<std::string> required_;
  std::optional<std::string> description_;
};

}  // namespace atria::openapi
