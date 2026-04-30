#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace atria {

struct JsonError {
  std::string message;
  std::size_t position{0};
};

struct JsonParseLimits {
  std::size_t max_depth{64};
  std::size_t max_string_length{std::size_t{1024} * std::size_t{1024}};
};

class Json {
 public:
  using ParseLimits = JsonParseLimits;

  enum class Type : std::uint8_t {
    Null,
    Bool,
    Int,
    Double,
    String,
    Array,
    Object,
  };

  using Array = std::vector<Json>;
  using Object = std::vector<std::pair<std::string, Json>>;

  Json() noexcept = default;
  Json(std::nullptr_t) noexcept;  // NOLINT(google-explicit-constructor)
  Json(bool value) noexcept;       // NOLINT(google-explicit-constructor)
  Json(int value) noexcept;        // NOLINT(google-explicit-constructor)
  Json(std::int64_t value) noexcept;  // NOLINT(google-explicit-constructor)
  Json(double value) noexcept;     // NOLINT(google-explicit-constructor)
  Json(const char* value);          // NOLINT(google-explicit-constructor)
  Json(std::string value);          // NOLINT(google-explicit-constructor)
  Json(std::string_view value);     // NOLINT(google-explicit-constructor)
  Json(Array value);                // NOLINT(google-explicit-constructor)
  Json(Object value);               // NOLINT(google-explicit-constructor)

  Json(std::initializer_list<std::pair<std::string, Json>> obj);

  static Json array(std::initializer_list<Json> items);
  static Json object(std::initializer_list<std::pair<std::string, Json>> entries);

  [[nodiscard]] Type type() const noexcept;

  [[nodiscard]] bool is_null() const noexcept { return type() == Type::Null; }
  [[nodiscard]] bool is_bool() const noexcept { return type() == Type::Bool; }
  [[nodiscard]] bool is_number() const noexcept;
  [[nodiscard]] bool is_int() const noexcept { return type() == Type::Int; }
  [[nodiscard]] bool is_double() const noexcept { return type() == Type::Double; }
  [[nodiscard]] bool is_string() const noexcept { return type() == Type::String; }
  [[nodiscard]] bool is_array() const noexcept { return type() == Type::Array; }
  [[nodiscard]] bool is_object() const noexcept { return type() == Type::Object; }

  [[nodiscard]] bool as_bool() const;
  [[nodiscard]] std::int64_t as_int() const;
  [[nodiscard]] double as_double() const;
  [[nodiscard]] const std::string& as_string() const;
  [[nodiscard]] const Array& as_array() const;
  [[nodiscard]] const Object& as_object() const;

  [[nodiscard]] const Json* find(std::string_view key) const;

  [[nodiscard]] static std::expected<Json, JsonError> parse(std::string_view text,
                                                            ParseLimits limits = {});

  [[nodiscard]] std::string stringify() const;

 private:
  using Variant = std::variant<std::monostate, bool, std::int64_t, double, std::string, Array,
                                Object>;
  Variant value_;
};

}  // namespace atria
