#include "atria/json.hpp"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace atria {

Json::Json(std::nullptr_t) noexcept : value_(std::monostate{}) {}

Json::Json(bool value) noexcept : value_(value) {}

Json::Json(int value) noexcept : value_(static_cast<std::int64_t>(value)) {}

Json::Json(std::int64_t value) noexcept : value_(value) {}

Json::Json(double value) noexcept : value_(value) {}

Json::Json(const char* value) : value_(std::string{value}) {}

Json::Json(std::string value) : value_(std::move(value)) {}

Json::Json(std::string_view value) : value_(std::string{value}) {}

Json::Json(Array value) : value_(std::move(value)) {}

Json::Json(Object value) : value_(std::move(value)) {}

Json::Json(std::initializer_list<std::pair<std::string, Json>> obj) {
  Object o;
  o.reserve(obj.size());
  for (auto& [k, v] : obj) {
    o.emplace_back(k, v);
  }
  value_ = std::move(o);
}

Json Json::array(std::initializer_list<Json> items) {
  Array a;
  a.reserve(items.size());
  for (auto& item : items) {
    a.push_back(item);
  }
  return Json{std::move(a)};
}

Json Json::object(std::initializer_list<std::pair<std::string, Json>> entries) {
  Object o;
  o.reserve(entries.size());
  for (auto& [k, v] : entries) {
    o.emplace_back(k, v);
  }
  return Json{std::move(o)};
}

Json::Type Json::type() const noexcept {
  switch (value_.index()) {
  case 0:
    return Type::Null;
  case 1:
    return Type::Bool;
  case 2:
    return Type::Int;
  case 3:
    return Type::Double;
  case 4:
    return Type::String;
  case 5:
    return Type::Array;
  case 6:
    return Type::Object;
  default:
    return Type::Null;
  }
}

bool Json::is_number() const noexcept {
  return is_int() || is_double();
}

bool Json::as_bool() const {
  return std::get<bool>(value_);
}

std::int64_t Json::as_int() const {
  if (is_double()) {
    return static_cast<std::int64_t>(std::get<double>(value_));
  }
  return std::get<std::int64_t>(value_);
}

double Json::as_double() const {
  if (is_int()) {
    return static_cast<double>(std::get<std::int64_t>(value_));
  }
  return std::get<double>(value_);
}

const std::string& Json::as_string() const {
  return std::get<std::string>(value_);
}

const Json::Array& Json::as_array() const {
  return std::get<Array>(value_);
}

const Json::Object& Json::as_object() const {
  return std::get<Object>(value_);
}

const Json* Json::find(std::string_view key) const {
  if (!is_object()) {
    return nullptr;
  }
  for (const auto& [k, v] : as_object()) {
    if (k == key) {
      return &v;
    }
  }
  return nullptr;
}

namespace {

[[nodiscard]] bool is_ascii_alnum(unsigned char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

[[nodiscard]] bool is_ascii_upper(unsigned char c) noexcept {
  return c >= 'A' && c <= 'Z';
}

[[nodiscard]] bool is_ascii_lower(unsigned char c) noexcept {
  return c >= 'a' && c <= 'z';
}

[[nodiscard]] bool is_ascii_digit(unsigned char c) noexcept {
  return c >= '0' && c <= '9';
}

[[nodiscard]] char ascii_lower(char c) noexcept {
  auto u = static_cast<unsigned char>(c);
  if (is_ascii_upper(u)) {
    return static_cast<char>(u + static_cast<unsigned char>('a' - 'A'));
  }
  return c;
}

[[nodiscard]] char ascii_upper(char c) noexcept {
  auto u = static_cast<unsigned char>(c);
  if (is_ascii_lower(u)) {
    return static_cast<char>(u - static_cast<unsigned char>('a' - 'A'));
  }
  return c;
}

[[nodiscard]] bool is_key_separator(unsigned char c) noexcept {
  return c == '_' || c == '-' || c == ' ';
}

[[nodiscard]] std::vector<std::string> split_key_words(std::string_view key) {
  std::vector<std::string> words;
  std::string current;

  for (std::size_t i = 0; i < key.size(); ++i) {
    auto c = static_cast<unsigned char>(key[i]);
    if (is_key_separator(c)) {
      if (!current.empty()) {
        words.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    if (!is_ascii_alnum(c)) {
      return {};
    }

    bool starts_new_word = false;
    if (!current.empty() && is_ascii_upper(c)) {
      auto previous = static_cast<unsigned char>(key[i - 1]);
      auto next = i + 1 < key.size() ? static_cast<unsigned char>(key[i + 1])
                                     : static_cast<unsigned char>(0);
      starts_new_word = is_ascii_lower(previous) || is_ascii_digit(previous) ||
                        (is_ascii_upper(previous) && is_ascii_lower(next));
    }
    if (starts_new_word) {
      words.push_back(std::move(current));
      current.clear();
    }
    current.push_back(ascii_lower(key[i]));
  }

  if (!current.empty()) {
    words.push_back(std::move(current));
  }
  return words;
}

[[nodiscard]] std::string
format_key_words(const std::vector<std::string>& words, JsonKeyStyle key_style) {
  if (words.empty()) {
    return {};
  }

  std::string out;
  switch (key_style) {
  case JsonKeyStyle::Preserve:
    break;
  case JsonKeyStyle::SnakeCase:
    for (std::size_t i = 0; i < words.size(); ++i) {
      if (i != 0) {
        out.push_back('_');
      }
      out.append(words[i]);
    }
    break;
  case JsonKeyStyle::CamelCase:
    out.append(words.front());
    for (std::size_t i = 1; i < words.size(); ++i) {
      if (!words[i].empty()) {
        out.push_back(ascii_upper(words[i].front()));
        out.append(words[i].substr(1));
      }
    }
    break;
  case JsonKeyStyle::PascalCase:
    for (const auto& word : words) {
      if (!word.empty()) {
        out.push_back(ascii_upper(word.front()));
        out.append(word.substr(1));
      }
    }
    break;
  }
  return out;
}

[[nodiscard]] std::string convert_key_style(std::string_view key, JsonKeyStyle key_style) {
  if (key_style == JsonKeyStyle::Preserve) {
    return std::string{key};
  }
  auto words = split_key_words(key);
  if (words.empty()) {
    return std::string{key};
  }
  return format_key_words(words, key_style);
}

class Parser {
public:
  Parser(std::string_view input, Json::ParseLimits limits, JsonKeyStyle key_style)
      : input_(input), limits_(limits), key_style_(key_style) {}

  std::expected<Json, JsonError> parse_root() {
    skip_ws();
    auto v = parse_value(0);
    if (!v.has_value()) {
      return std::unexpected(v.error());
    }
    skip_ws();
    if (pos_ != input_.size()) {
      return std::unexpected(JsonError{"trailing content", pos_});
    }
    return v;
  }

private:
  std::string_view input_;
  std::size_t pos_{0};
  Json::ParseLimits limits_;
  JsonKeyStyle key_style_{JsonKeyStyle::Preserve};

  void skip_ws() {
    while (pos_ < input_.size()) {
      char c = input_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  std::expected<Json, JsonError> parse_value(std::size_t depth) {
    if (depth > limits_.max_depth) {
      return std::unexpected(JsonError{"max nesting depth exceeded", pos_});
    }
    skip_ws();
    if (pos_ >= input_.size()) {
      return std::unexpected(JsonError{"unexpected end of input", pos_});
    }
    char c = input_[pos_];
    if (c == '{') {
      return parse_object(depth);
    }
    if (c == '[') {
      return parse_array(depth);
    }
    if (c == '"') {
      auto s = parse_string();
      if (!s.has_value()) {
        return std::unexpected(s.error());
      }
      return Json{std::move(*s)};
    }
    if (c == 't' || c == 'f') {
      return parse_bool();
    }
    if (c == 'n') {
      return parse_null();
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return parse_number();
    }
    return std::unexpected(JsonError{"unexpected character", pos_});
  }

  std::expected<Json, JsonError> parse_null() {
    if (input_.substr(pos_, 4) != "null") {
      return std::unexpected(JsonError{"invalid literal", pos_});
    }
    pos_ += 4;
    return Json{nullptr};
  }

  std::expected<Json, JsonError> parse_bool() {
    if (input_.substr(pos_, 4) == "true") {
      pos_ += 4;
      return Json{true};
    }
    if (input_.substr(pos_, 5) == "false") {
      pos_ += 5;
      return Json{false};
    }
    return std::unexpected(JsonError{"invalid literal", pos_});
  }

  std::expected<Json, JsonError> parse_number() {
    std::size_t start = pos_;
    bool is_float = false;
    if (input_[pos_] == '-') {
      ++pos_;
    }
    if (pos_ >= input_.size()) {
      return std::unexpected(JsonError{"truncated number", pos_});
    }
    if (input_[pos_] == '0') {
      ++pos_;
    } else if (input_[pos_] >= '1' && input_[pos_] <= '9') {
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0) {
        ++pos_;
      }
    } else {
      return std::unexpected(JsonError{"invalid number", pos_});
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      is_float = true;
      ++pos_;
      if (pos_ >= input_.size() || std::isdigit(static_cast<unsigned char>(input_[pos_])) == 0) {
        return std::unexpected(JsonError{"invalid fraction", pos_});
      }
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0) {
        ++pos_;
      }
    }
    if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
      is_float = true;
      ++pos_;
      if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= input_.size() || std::isdigit(static_cast<unsigned char>(input_[pos_])) == 0) {
        return std::unexpected(JsonError{"invalid exponent", pos_});
      }
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0) {
        ++pos_;
      }
    }
    std::string token{input_.substr(start, pos_ - start)};
    if (is_float) {
      try {
        std::size_t consumed = 0;
        double d = std::stod(token, &consumed);
        return Json{d};
      } catch (...) {
        return std::unexpected(JsonError{"number out of range", start});
      }
    }
    try {
      std::size_t consumed = 0;
      long long ll = std::stoll(token, &consumed);
      return Json{static_cast<std::int64_t>(ll)};
    } catch (...) {
      try {
        std::size_t consumed = 0;
        double d = std::stod(token, &consumed);
        return Json{d};
      } catch (...) {
        return std::unexpected(JsonError{"number out of range", start});
      }
    }
  }

  std::expected<std::string, JsonError> parse_string() {
    if (pos_ >= input_.size() || input_[pos_] != '"') {
      return std::unexpected(JsonError{"expected string", pos_});
    }
    ++pos_;
    std::string out;
    while (pos_ < input_.size()) {
      char c = input_[pos_];
      if (c == '"') {
        ++pos_;
        if (out.size() > limits_.max_string_length) {
          return std::unexpected(JsonError{"string too long", pos_});
        }
        return out;
      }
      if (c == '\\') {
        if (pos_ + 1 >= input_.size()) {
          return std::unexpected(JsonError{"truncated escape", pos_});
        }
        char esc = input_[pos_ + 1];
        switch (esc) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          if (pos_ + 5 >= input_.size()) {
            return std::unexpected(JsonError{"truncated unicode escape", pos_});
          }
          unsigned code = 0;
          for (std::size_t i = 0; i < 4; ++i) {
            char h = input_[pos_ + 2 + i];
            unsigned digit = 0;
            if (h >= '0' && h <= '9') {
              digit = static_cast<unsigned>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
              digit = 10 + static_cast<unsigned>(h - 'a');
            } else if (h >= 'A' && h <= 'F') {
              digit = 10 + static_cast<unsigned>(h - 'A');
            } else {
              return std::unexpected(JsonError{"invalid unicode escape", pos_});
            }
            code = (code << 4) | digit;
          }
          pos_ += 4;
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          return std::unexpected(JsonError{"invalid escape", pos_});
        }
        pos_ += 2;
        continue;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        return std::unexpected(JsonError{"control character in string", pos_});
      }
      out.push_back(c);
      ++pos_;
      if (out.size() > limits_.max_string_length) {
        return std::unexpected(JsonError{"string too long", pos_});
      }
    }
    return std::unexpected(JsonError{"unterminated string", pos_});
  }

  std::expected<Json, JsonError> parse_array(std::size_t depth) {
    ++pos_;
    Json::Array out;
    skip_ws();
    if (pos_ < input_.size() && input_[pos_] == ']') {
      ++pos_;
      return Json{std::move(out)};
    }
    while (true) {
      auto v = parse_value(depth + 1);
      if (!v.has_value()) {
        return std::unexpected(v.error());
      }
      out.push_back(std::move(*v));
      skip_ws();
      if (pos_ >= input_.size()) {
        return std::unexpected(JsonError{"unterminated array", pos_});
      }
      if (input_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (input_[pos_] == ']') {
        ++pos_;
        return Json{std::move(out)};
      }
      return std::unexpected(JsonError{"expected , or ]", pos_});
    }
  }

  std::expected<Json, JsonError> parse_object(std::size_t depth) {
    ++pos_;
    Json::Object out;
    skip_ws();
    if (pos_ < input_.size() && input_[pos_] == '}') {
      ++pos_;
      return Json{std::move(out)};
    }
    while (true) {
      skip_ws();
      auto key = parse_string();
      if (!key.has_value()) {
        return std::unexpected(key.error());
      }
      skip_ws();
      if (pos_ >= input_.size() || input_[pos_] != ':') {
        return std::unexpected(JsonError{"expected ':' after key", pos_});
      }
      ++pos_;
      auto v = parse_value(depth + 1);
      if (!v.has_value()) {
        return std::unexpected(v.error());
      }
      out.emplace_back(convert_key_style(*key, key_style_), std::move(*v));
      skip_ws();
      if (pos_ >= input_.size()) {
        return std::unexpected(JsonError{"unterminated object", pos_});
      }
      if (input_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (input_[pos_] == '}') {
        ++pos_;
        return Json{std::move(out)};
      }
      return std::unexpected(JsonError{"expected , or }", pos_});
    }
  }
};

void escape_string(std::string& out, std::string_view s) {
  out.push_back('"');
  for (char c : s) {
    auto u = static_cast<unsigned char>(c);
    switch (c) {
    case '"':
      out.append("\\\"");
      break;
    case '\\':
      out.append("\\\\");
      break;
    case '\b':
      out.append("\\b");
      break;
    case '\f':
      out.append("\\f");
      break;
    case '\n':
      out.append("\\n");
      break;
    case '\r':
      out.append("\\r");
      break;
    case '\t':
      out.append("\\t");
      break;
    default:
      if (u < 0x20) {
        char buf[8] = {0};
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(u));
        out.append(buf);
      } else {
        out.push_back(c);
      }
    }
  }
  out.push_back('"');
}

void stringify_into(std::string& out, const Json& j, JsonKeyStyle key_style) {
  using Type = Json::Type;
  switch (j.type()) {
  case Type::Null:
    out.append("null");
    break;
  case Type::Bool:
    out.append(j.as_bool() ? "true" : "false");
    break;
  case Type::Int:
    out.append(std::to_string(j.as_int()));
    break;
  case Type::Double: {
    double d = j.as_double();
    if (!std::isfinite(d)) {
      out.append("null");
      break;
    }
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    out.append(buf);
    break;
  }
  case Type::String:
    escape_string(out, j.as_string());
    break;
  case Type::Array: {
    out.push_back('[');
    bool first = true;
    for (const auto& item : j.as_array()) {
      if (!first) {
        out.push_back(',');
      }
      first = false;
      stringify_into(out, item, key_style);
    }
    out.push_back(']');
    break;
  }
  case Type::Object: {
    out.push_back('{');
    bool first = true;
    for (const auto& [k, v] : j.as_object()) {
      if (!first) {
        out.push_back(',');
      }
      first = false;
      escape_string(out, convert_key_style(k, key_style));
      out.push_back(':');
      stringify_into(out, v, key_style);
    }
    out.push_back('}');
    break;
  }
  }
}

}  // namespace

std::expected<Json, JsonError> Json::parse(std::string_view text, ParseLimits limits) {
  Parser p{text, limits, JsonKeyStyle::Preserve};
  return p.parse_root();
}

std::expected<Json, JsonError>
Json::parse(std::string_view text, ParseLimits limits, JsonKeyStyle key_style) {
  Parser p{text, limits, key_style};
  return p.parse_root();
}

std::string Json::stringify() const {
  return stringify(JsonKeyStyle::Preserve);
}

std::string Json::stringify(JsonKeyStyle key_style) const {
  std::string out;
  stringify_into(out, *this, key_style);
  return out;
}

}  // namespace atria
