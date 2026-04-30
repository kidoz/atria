#pragma once

#include <cstdint>
#include <string_view>

namespace atria {

enum class Method : std::uint8_t {
  Get,
  Post,
  Put,
  Patch,
  Delete,
  Options,
  Head,
};

[[nodiscard]] std::string_view to_string(Method method) noexcept;
[[nodiscard]] bool parse_method(std::string_view text, Method& out) noexcept;

}  // namespace atria
