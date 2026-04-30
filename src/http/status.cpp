#include "atria/status.hpp"

#include <cstdint>
#include <string_view>

namespace atria {

std::string_view reason_phrase(Status status) noexcept {
  switch (status) {
  case Status::SwitchingProtocols:
    return "Switching Protocols";
  case Status::Ok:
    return "OK";
  case Status::Created:
    return "Created";
  case Status::NoContent:
    return "No Content";
  case Status::BadRequest:
    return "Bad Request";
  case Status::Unauthorized:
    return "Unauthorized";
  case Status::Forbidden:
    return "Forbidden";
  case Status::NotFound:
    return "Not Found";
  case Status::MethodNotAllowed:
    return "Method Not Allowed";
  case Status::PayloadTooLarge:
    return "Payload Too Large";
  case Status::UnsupportedMediaType:
    return "Unsupported Media Type";
  case Status::UnprocessableEntity:
    return "Unprocessable Entity";
  case Status::InternalServerError:
    return "Internal Server Error";
  case Status::NotImplemented:
    return "Not Implemented";
  }
  return "Unknown";
}

std::uint16_t status_code(Status status) noexcept {
  return static_cast<std::uint16_t>(status);
}

}  // namespace atria
