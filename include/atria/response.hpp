#pragma once

#include "atria/headers.hpp"
#include "atria/status.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace atria {

class Json;

// Pull-based chunk source for streaming responses. The runtime calls the provider on the
// loop thread when the connection is ready to send more bytes. Returning std::nullopt (or
// an empty string) signals end-of-stream.
//
// Use cases: file downloads, large JSON exports, server-sent events. The provider closure
// owns any state it needs (file handle, cursor, queue) and is destroyed when the
// Connection ends.
//
// Thread-safety: the provider is invoked on the runtime's event-loop thread. It must not
// block or perform long-running work — that defeats the entire event-loop model. If the
// data source is slow, run a worker thread that pushes into a thread-safe queue and have
// the provider drain that queue.
using ChunkProvider = std::function<std::optional<std::string>()>;

class Response {
 public:
  Response() = default;
  explicit Response(Status status, std::string body = {});

  static Response text(std::string body, Status status = Status::Ok);
  static Response json(const Json& value, Status status = Status::Ok);
  static Response empty(Status status = Status::NoContent);

  // Streaming factory. The body is produced lazily by `provider`. If `content_length` is
  // supplied, the response uses Content-Length framing; otherwise the runtime selects
  // Transfer-Encoding: chunked for HTTP/1.1 clients (and Connection: close for HTTP/1.0).
  static Response stream(ChunkProvider provider,
                          std::optional<std::size_t> content_length = std::nullopt,
                          Status status = Status::Ok);

  [[nodiscard]] Status status() const noexcept { return status_; }
  [[nodiscard]] const Headers& headers() const noexcept { return headers_; }
  [[nodiscard]] Headers& headers() noexcept { return headers_; }
  [[nodiscard]] const std::string& body() const noexcept { return body_; }

  [[nodiscard]] bool is_streaming() const noexcept { return static_cast<bool>(chunk_provider_); }
  [[nodiscard]] std::optional<std::size_t> content_length() const noexcept {
    return content_length_;
  }

  // Hand the provider over to the runtime; subsequent calls return an empty function.
  [[nodiscard]] ChunkProvider take_chunk_provider() noexcept {
    return std::exchange(chunk_provider_, ChunkProvider{});
  }

  Response& set_status(Status status) noexcept;
  Response& set_header(std::string name, std::string value);
  Response& set_body(std::string body);

  // Serialize headers + (for non-streaming) full body. Streaming responses still pass
  // through this method but the runtime should read `is_streaming()` first and use
  // `serialize_headers()` instead so it can drive chunk emission.
  [[nodiscard]] std::string serialize() const;

  // Serialize only the status line + headers (terminating CRLF). Used by the runtime when
  // a streaming response is being sent: write the headers, then loop pulling chunks.
  [[nodiscard]] std::string serialize_headers() const;

 private:
  Status status_{Status::Ok};
  Headers headers_;
  std::string body_;

  ChunkProvider chunk_provider_;
  std::optional<std::size_t> content_length_;
};

}  // namespace atria
