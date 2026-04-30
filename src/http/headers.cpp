#include "atria/headers.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace atria {

namespace {

bool ascii_iequal(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    auto a = static_cast<unsigned char>(lhs[i]);
    auto b = static_cast<unsigned char>(rhs[i]);
    if (std::tolower(a) != std::tolower(b)) {
      return false;
    }
  }
  return true;
}

bool is_token_char(unsigned char c) noexcept {
  if ((std::isalnum(c) != 0)) {
    return true;
  }
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

}  // namespace

void Headers::set(std::string name, std::string value) {
  auto it = std::find_if(entries_.begin(), entries_.end(),
                          [&](const Entry& e) { return ascii_iequal(e.first, name); });
  if (it != entries_.end()) {
    it->second = std::move(value);
    return;
  }
  entries_.emplace_back(std::move(name), std::move(value));
}

void Headers::append(std::string name, std::string value) {
  entries_.emplace_back(std::move(name), std::move(value));
}

std::optional<std::string_view> Headers::find(std::string_view name) const {
  for (const auto& [k, v] : entries_) {
    if (ascii_iequal(k, name)) {
      return v;
    }
  }
  return std::nullopt;
}

bool Headers::valid_name(std::string_view name) noexcept {
  if (name.empty()) {
    return false;
  }
  for (char c : name) {
    if (!is_token_char(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

bool Headers::valid_value(std::string_view value) noexcept {
  for (char c : value) {
    auto u = static_cast<unsigned char>(c);
    if (u == '\r' || u == '\n') {
      return false;
    }
    if (u < 0x20 && u != '\t') {
      return false;
    }
  }
  return true;
}

}  // namespace atria
