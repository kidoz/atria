#include "net/websocket_protocol.hpp"

#include "atria/websocket.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace atria::net::websocket {

namespace {

constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] std::optional<unsigned char> decode_base64_char(char c) noexcept {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<unsigned char>(c - 'A');
  }
  if (c >= 'a' && c <= 'z') {
    return static_cast<unsigned char>(26 + (c - 'a'));
  }
  if (c >= '0' && c <= '9') {
    return static_cast<unsigned char>(52 + (c - '0'));
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<unsigned char>> decode_base64(std::string_view input) {
  if (input.empty() || input.size() % 4 != 0) {
    return std::nullopt;
  }

  std::vector<unsigned char> out;
  out.reserve((input.size() / 4) * 3);
  for (std::size_t i = 0; i < input.size(); i += 4) {
    std::array<unsigned char, 4> values{};
    std::size_t padding = 0;
    for (std::size_t j = 0; j < 4; ++j) {
      char c = input[i + j];
      if (c == '=') {
        values[j] = 0;
        ++padding;
        continue;
      }
      if (padding != 0) {
        return std::nullopt;
      }
      auto decoded = decode_base64_char(c);
      if (!decoded.has_value()) {
        return std::nullopt;
      }
      values[j] = *decoded;
    }
    if (padding > 2) {
      return std::nullopt;
    }
    out.push_back(static_cast<unsigned char>((values[0] << 2U) | (values[1] >> 4U)));
    if (padding < 2) {
      out.push_back(static_cast<unsigned char>(((values[1] & 0x0FU) << 4U) | (values[2] >> 2U)));
    }
    if (padding == 0) {
      out.push_back(static_cast<unsigned char>(((values[2] & 0x03U) << 6U) | values[3]));
    }
  }
  return out;
}

[[nodiscard]] std::string encode_base64(std::span<const unsigned char> input) {
  std::string out;
  out.reserve(((input.size() + 2U) / 3U) * 4U);
  for (std::size_t i = 0; i < input.size(); i += 3) {
    std::uint32_t chunk = static_cast<std::uint32_t>(input[i]) << 16U;
    bool has_second = i + 1 < input.size();
    bool has_third = i + 2 < input.size();
    if (has_second) {
      chunk |= static_cast<std::uint32_t>(input[i + 1]) << 8U;
    }
    if (has_third) {
      chunk |= static_cast<std::uint32_t>(input[i + 2]);
    }
    out.push_back(kBase64Alphabet[(chunk >> 18U) & 0x3FU]);
    out.push_back(kBase64Alphabet[(chunk >> 12U) & 0x3FU]);
    out.push_back(has_second ? kBase64Alphabet[(chunk >> 6U) & 0x3FU] : '=');
    out.push_back(has_third ? kBase64Alphabet[chunk & 0x3FU] : '=');
  }
  return out;
}

class Sha1 {
public:
  void update(std::string_view data) {
    for (char raw : data) {
      auto c = static_cast<unsigned char>(raw);
      buffer_[buffer_size_++] = c;
      message_size_bits_ += 8;
      if (buffer_size_ == buffer_.size()) {
        process_block(buffer_);
        buffer_size_ = 0;
      }
    }
  }

  [[nodiscard]] std::array<unsigned char, 20> final() {
    buffer_[buffer_size_++] = 0x80;
    if (buffer_size_ > 56) {
      while (buffer_size_ < buffer_.size()) {
        buffer_[buffer_size_++] = 0;
      }
      process_block(buffer_);
      buffer_size_ = 0;
    }
    while (buffer_size_ < 56) {
      buffer_[buffer_size_++] = 0;
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
      buffer_[buffer_size_++] = static_cast<unsigned char>((message_size_bits_ >> shift) & 0xFFU);
    }
    process_block(buffer_);

    std::array<unsigned char, 20> digest{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      digest[i * 4] = static_cast<unsigned char>((state_[i] >> 24U) & 0xFFU);
      digest[i * 4 + 1] = static_cast<unsigned char>((state_[i] >> 16U) & 0xFFU);
      digest[i * 4 + 2] = static_cast<unsigned char>((state_[i] >> 8U) & 0xFFU);
      digest[i * 4 + 3] = static_cast<unsigned char>(state_[i] & 0xFFU);
    }
    return digest;
  }

private:
  void process_block(const std::array<unsigned char, 64>& block) {
    std::array<std::uint32_t, 80> words{};
    for (std::size_t i = 0; i < 16; ++i) {
      words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24U) |
                 (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16U) |
                 (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8U) |
                 static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
      words[i] = std::rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];

    for (std::size_t i = 0; i < words.size(); ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999U;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1U;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCU;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6U;
      }
      std::uint32_t temp = std::rotl(a, 5) + f + e + k + words[i];
      e = d;
      d = c;
      c = std::rotl(b, 30);
      b = a;
      a = temp;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
  }

  std::array<std::uint32_t, 5>
      state_{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
  std::array<unsigned char, 64> buffer_{};
  std::size_t buffer_size_{0};
  std::uint64_t message_size_bits_{0};
};

[[nodiscard]] std::uint64_t read_be(std::string_view data, std::size_t offset, std::size_t bytes) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < bytes; ++i) {
    value = (value << 8U) | static_cast<unsigned char>(data[offset + i]);
  }
  return value;
}

void append_be(std::string& out, std::uint64_t value, std::size_t bytes) {
  for (std::size_t i = 0; i < bytes; ++i) {
    auto shift = static_cast<unsigned>((bytes - i - 1U) * 8U);
    out.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

[[nodiscard]] bool is_known_data_opcode(std::uint8_t opcode) noexcept {
  return opcode == static_cast<std::uint8_t>(Opcode::Continuation) ||
         opcode == static_cast<std::uint8_t>(Opcode::Text) ||
         opcode == static_cast<std::uint8_t>(Opcode::Binary);
}

[[nodiscard]] bool is_known_control_opcode(std::uint8_t opcode) noexcept {
  return opcode == static_cast<std::uint8_t>(Opcode::Close) ||
         opcode == static_cast<std::uint8_t>(Opcode::Ping) ||
         opcode == static_cast<std::uint8_t>(Opcode::Pong);
}

}  // namespace

std::expected<std::string, HandshakeError> make_accept_key(std::string_view client_key) {
  auto decoded = decode_base64(client_key);
  if (!decoded.has_value() || decoded->size() != 16) {
    return std::unexpected(HandshakeError{"invalid Sec-WebSocket-Key"});
  }

  Sha1 sha1;
  sha1.update(client_key);
  sha1.update(kWebSocketGuid);
  auto digest = sha1.final();
  return encode_base64(digest);
}

ParseResult parse_frame(std::string_view buffer, std::size_t max_frame_bytes) {
  if (buffer.size() < 2) {
    return {};
  }

  auto first = static_cast<unsigned char>(buffer[0]);
  auto second = static_cast<unsigned char>(buffer[1]);
  bool fin = (first & 0x80U) != 0;
  bool rsv = (first & 0x70U) != 0;
  auto opcode_value = static_cast<std::uint8_t>(first & 0x0FU);
  bool masked = (second & 0x80U) != 0;
  std::uint64_t payload_len = second & 0x7FU;
  std::size_t offset = 2;

  if (rsv || (!is_known_data_opcode(opcode_value) && !is_known_control_opcode(opcode_value))) {
    return {
        ParseStatus::ProtocolError,
        {},
        0,
        WebSocketCloseCode::ProtocolError,
        "invalid websocket opcode"
    };
  }
  if (!masked) {
    return {
        ParseStatus::ProtocolError,
        {},
        0,
        WebSocketCloseCode::ProtocolError,
        "client frame was not masked"
    };
  }
  if (payload_len == 126) {
    if (buffer.size() < offset + 2) {
      return {};
    }
    payload_len = read_be(buffer, offset, 2);
    offset += 2;
  } else if (payload_len == 127) {
    if (buffer.size() < offset + 8) {
      return {};
    }
    payload_len = read_be(buffer, offset, 8);
    offset += 8;
  }
  if (payload_len > max_frame_bytes) {
    return {
        ParseStatus::MessageTooBig,
        {},
        0,
        WebSocketCloseCode::MessageTooBig,
        "websocket frame too large"
    };
  }
  if (payload_len > static_cast<std::uint64_t>(buffer.size())) {
    return {};
  }
  if (buffer.size() < offset + 4) {
    return {};
  }
  std::array<unsigned char, 4> mask{
      static_cast<unsigned char>(buffer[offset]),
      static_cast<unsigned char>(buffer[offset + 1]),
      static_cast<unsigned char>(buffer[offset + 2]),
      static_cast<unsigned char>(buffer[offset + 3]),
  };
  offset += 4;
  if (buffer.size() < offset + payload_len) {
    return {};
  }

  bool is_control = is_known_control_opcode(opcode_value);
  if (is_control && (!fin || payload_len > 125)) {
    return {
        ParseStatus::ProtocolError,
        {},
        0,
        WebSocketCloseCode::ProtocolError,
        "invalid websocket control frame"
    };
  }

  std::string payload;
  payload.resize(static_cast<std::size_t>(payload_len));
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<char>(static_cast<unsigned char>(buffer[offset + i]) ^ mask[i % 4]);
  }

  return {
      .status = ParseStatus::Frame,
      .frame = Frame{fin, static_cast<Opcode>(opcode_value), std::move(payload)},
      .consumed = offset + static_cast<std::size_t>(payload_len),
  };
}

std::string encode_frame(Opcode opcode, std::string_view payload) {
  std::string out;
  out.reserve(payload.size() + 14);
  out.push_back(static_cast<char>(0x80U | static_cast<std::uint8_t>(opcode)));
  if (payload.size() <= 125) {
    out.push_back(static_cast<char>(payload.size()));
  } else if (payload.size() <= 0xFFFFU) {
    out.push_back(126);
    append_be(out, payload.size(), 2);
  } else {
    out.push_back(127);
    append_be(out, payload.size(), 8);
  }
  out.append(payload);
  return out;
}

std::string encode_close_frame(WebSocketCloseCode code, std::string_view reason) {
  std::string payload;
  auto numeric = static_cast<std::uint16_t>(code);
  payload.push_back(static_cast<char>((numeric >> 8U) & 0xFFU));
  payload.push_back(static_cast<char>(numeric & 0xFFU));
  payload.append(reason.substr(0, 123));
  return encode_frame(Opcode::Close, payload);
}

}  // namespace atria::net::websocket
