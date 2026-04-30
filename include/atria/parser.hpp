#pragma once

#include "atria/request.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace atria {

struct HttpError {
  Status status{Status::BadRequest};
  std::string message;
  // True when parse_request returned because more bytes are needed (e.g. headers not
  // yet terminated, body incomplete, last chunk not yet seen). The runtime should read
  // more bytes from the connection and try again. False means a permanent protocol error.
  bool incomplete{false};
};

struct ParseLimits {
  std::size_t max_request_line_bytes{std::size_t{8} * 1024};
  std::size_t max_header_bytes{std::size_t{16} * 1024};
  std::size_t max_header_count{64};
  std::size_t max_body_bytes{std::size_t{1024} * 1024};
};

[[nodiscard]] inline ParseLimits parse_limits_from(const ServerConfig& config) {
  return {
      .max_request_line_bytes = config.max_request_line_bytes,
      .max_header_bytes = config.max_header_bytes,
      .max_header_count = config.max_header_count,
      .max_body_bytes = config.max_body_bytes,
  };
}

// Parse a single HTTP/1.1 request from `buffer`. If `consumed` is non-null, on success it
// is set to the number of bytes consumed from `buffer` (allows pipelining: the rest of the
// buffer can be used for the next request). On `HttpError{incomplete=true}`, the caller
// should read more bytes and retry. On `HttpError{incomplete=false}` the connection is in
// an unrecoverable state and should be closed (after the runtime sends an error response).
[[nodiscard]] std::expected<Request, HttpError> parse_request(std::string_view buffer,
                                                              const ParseLimits& limits,
                                                              std::size_t* consumed = nullptr);

}  // namespace atria
