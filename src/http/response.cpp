#include "atria/response.hpp"

#include "atria/headers.hpp"
#include "atria/json.hpp"
#include "atria/status.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace atria {

namespace {

[[nodiscard]] bool ascii_iequal(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    auto a = static_cast<unsigned char>(lhs[i]);
    auto b = static_cast<unsigned char>(rhs[i]);
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<unsigned char>(a + ('a' - 'A'));
    }
    if (b >= 'A' && b <= 'Z') {
      b = static_cast<unsigned char>(b + ('a' - 'A'));
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool has_header(const Headers& h, std::string_view name) {
  for (const auto& [k, v] : h.entries()) {
    if (ascii_iequal(k, name)) {
      return true;
    }
  }
  return false;
}

void append_status_line(std::string& out, Status status) {
  out.append("HTTP/1.1 ");
  out.append(std::to_string(status_code(status)));
  out.append(" ");
  out.append(reason_phrase(status));
  out.append("\r\n");
}

void append_filtered_headers(std::string& out, const Headers& headers) {
  for (const auto& [k, v] : headers.entries()) {
    if (Headers::valid_name(k) && Headers::valid_value(v)) {
      out.append(k);
      out.append(": ");
      out.append(v);
      out.append("\r\n");
    }
  }
}

}  // namespace

Response::Response(Status status, std::string body) : status_(status), body_(std::move(body)) {}

Response Response::text(std::string body, Status status) {
  Response r{status, std::move(body)};
  r.headers_.set("Content-Type", "text/plain; charset=utf-8");
  return r;
}

Response Response::json(const Json& value, Status status) {
  Response r{status, value.stringify()};
  r.headers_.set("Content-Type", "application/json; charset=utf-8");
  return r;
}

Response Response::json(const Json& value, JsonKeyStyle key_style, Status status) {
  Response r{status, value.stringify(key_style)};
  r.headers_.set("Content-Type", "application/json; charset=utf-8");
  return r;
}

Response Response::empty(Status status) {
  return Response{status};
}

Response
Response::stream(ChunkProvider provider, std::optional<std::size_t> content_length, Status status) {
  Response r{status};
  r.chunk_provider_ = std::move(provider);
  r.content_length_ = content_length;
  if (!has_header(r.headers_, "Content-Type")) {
    r.headers_.set("Content-Type", "application/octet-stream");
  }
  return r;
}

Response Response::stream_wakeable(
    WakeableChunkProvider provider,
    std::optional<std::size_t> content_length,
    Status status
) {
  Response r{status};
  r.wakeable_chunk_provider_ = std::move(provider);
  r.content_length_ = content_length;
  if (!has_header(r.headers_, "Content-Type")) {
    r.headers_.set("Content-Type", "application/octet-stream");
  }
  return r;
}

void StreamWaker::wake() const {
  if (wake_) {
    wake_();
  }
}

Response& Response::set_status(Status status) noexcept {
  status_ = status;
  return *this;
}

Response& Response::set_header(std::string name, std::string value) {
  headers_.set(std::move(name), std::move(value));
  return *this;
}

Response& Response::set_body(std::string body) {
  body_ = std::move(body);
  return *this;
}

std::string Response::serialize() const {
  // Streaming responses are emitted by the runtime in pieces. If a caller serializes a
  // streaming response directly (e.g. unit tests), they get headers + an empty body.
  std::string out;
  out.reserve(64 + body_.size());
  append_status_line(out, status_);
  append_filtered_headers(out, headers_);
  if (!is_streaming() && !has_header(headers_, "Content-Length")) {
    out.append("Content-Length: ");
    out.append(std::to_string(body_.size()));
    out.append("\r\n");
  }
  out.append("\r\n");
  if (!is_streaming()) {
    out.append(body_);
  }
  return out;
}

std::string Response::serialize_headers() const {
  std::string out;
  out.reserve(128);
  append_status_line(out, status_);
  append_filtered_headers(out, headers_);
  out.append("\r\n");
  return out;
}

}  // namespace atria
