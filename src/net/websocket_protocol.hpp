#pragma once

#include "atria/websocket.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace atria::net::websocket {

enum class Opcode : std::uint8_t {
  Continuation = 0x0,
  Text = 0x1,
  Binary = 0x2,
  Close = 0x8,
  Ping = 0x9,
  Pong = 0xA,
};

struct HandshakeError {
  std::string message;
};

struct Frame {
  bool fin{true};
  Opcode opcode{Opcode::Text};
  std::string payload;
};

enum class ParseStatus : std::uint8_t {
  Incomplete,
  Frame,
  ProtocolError,
  MessageTooBig,
};

struct ParseResult {
  ParseStatus status{ParseStatus::Incomplete};
  Frame frame;
  std::size_t consumed{0};
  WebSocketCloseCode close_code{WebSocketCloseCode::ProtocolError};
  std::string message;
};

[[nodiscard]] std::expected<std::string, HandshakeError>
make_accept_key(std::string_view client_key);

[[nodiscard]] ParseResult parse_frame(std::string_view buffer, std::size_t max_frame_bytes);

[[nodiscard]] bool is_valid_utf8(std::string_view data) noexcept;

[[nodiscard]] std::string encode_frame(Opcode opcode, std::string_view payload);
[[nodiscard]] std::string encode_close_frame(WebSocketCloseCode code, std::string_view reason);

}  // namespace atria::net::websocket
