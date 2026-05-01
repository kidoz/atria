#pragma once

#include <cstdint>
#include <string_view>

namespace atria {

enum class Status : std::uint16_t {
  SwitchingProtocols = 101,
  Ok = 200,
  Created = 201,
  NoContent = 204,
  PartialContent = 206,
  BadRequest = 400,
  Unauthorized = 401,
  Forbidden = 403,
  NotFound = 404,
  MethodNotAllowed = 405,
  PayloadTooLarge = 413,
  UnsupportedMediaType = 415,
  RangeNotSatisfiable = 416,
  UnprocessableEntity = 422,
  InternalServerError = 500,
  NotImplemented = 501,
};

[[nodiscard]] std::string_view reason_phrase(Status status) noexcept;
[[nodiscard]] std::uint16_t status_code(Status status) noexcept;

}  // namespace atria
