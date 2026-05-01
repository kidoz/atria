#include "atria/response.hpp"

#include "atria/headers.hpp"
#include "atria/json.hpp"
#include "atria/method.hpp"
#include "atria/range.hpp"
#include "atria/request.hpp"
#include "atria/status.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
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

Response Response::xml(std::string body, Status status) {
  Response r{status, std::move(body)};
  r.headers_.set("Content-Type", "text/xml; charset=utf-8");
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

Response Response::file(
    const Request& request,
    const std::filesystem::path& path,
    FileResponseOptions options
) {
  if (request.method() != Method::Get && request.method() != Method::Head) {
    Response response = Response::empty(Status::MethodNotAllowed);
    response.set_header("Allow", "GET, HEAD");
    return response;
  }

  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return Response::empty(Status::NotFound);
  }
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return Response::empty(Status::NotFound);
  }
  const std::uint64_t representation_size = static_cast<std::uint64_t>(file_size);
  if (representation_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Response::empty(Status::InternalServerError);
  }

  Status status = Status::Ok;
  ByteRange range{0, representation_size == 0 ? 0 : representation_size - 1U};
  bool has_range = false;
  if (options.allow_range) {
    auto range_header = request.header("Range");
    if (range_header.has_value()) {
      auto parsed = parse_byte_range(*range_header, representation_size);
      if (!parsed.has_value()) {
        Response response = Response::empty(Status::RangeNotSatisfiable);
        response.set_header("Accept-Ranges", "bytes");
        response.set_header("Content-Range", unsatisfied_content_range_value(representation_size));
        response.set_header("Content-Length", "0");
        return response;
      }
      range = *parsed;
      has_range = true;
      status = Status::PartialContent;
    }
  }

  const std::uint64_t content_length64 = representation_size == 0 ? 0 : range.size();
  if (content_length64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Response::empty(Status::InternalServerError);
  }
  if (range.first > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return Response::empty(Status::InternalServerError);
  }
  const auto content_length = static_cast<std::size_t>(content_length64);

  if (request.method() == Method::Head) {
    Response response{status};
    response.set_header("Content-Type", options.content_type);
    response.set_header("Accept-Ranges", "bytes");
    response.set_header("Content-Length", std::to_string(content_length));
    if (has_range) {
      response.set_header("Content-Range", content_range_value(range, representation_size));
    }
    return response;
  }

  auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
  if (!file->is_open()) {
    return Response::empty(Status::NotFound);
  }
  file->seekg(static_cast<std::streamoff>(range.first), std::ios::beg);
  if (!*file && content_length != 0) {
    return Response::empty(Status::NotFound);
  }

  auto remaining = std::make_shared<std::uint64_t>(content_length64);
  const std::size_t chunk_size =
      options.chunk_size == 0 ? std::size_t{64} * 1024 : options.chunk_size;
  const std::size_t max_stream_read =
      static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());
  const std::size_t effective_chunk_size =
      chunk_size > max_stream_read ? max_stream_read : chunk_size;
  ChunkProvider provider = [file, remaining, effective_chunk_size]() -> std::optional<std::string> {
    if (*remaining == 0 || !file->good()) {
      return std::nullopt;
    }
    const auto next_size = static_cast<std::size_t>(
        *remaining < static_cast<std::uint64_t>(effective_chunk_size) ? *remaining
                                                                      : effective_chunk_size
    );
    std::string chunk(next_size, '\0');
    file->read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const auto read_count = file->gcount();
    if (read_count <= 0) {
      *remaining = 0;
      return std::nullopt;
    }
    chunk.resize(static_cast<std::size_t>(read_count));
    *remaining -= static_cast<std::uint64_t>(chunk.size());
    return chunk;
  };

  Response response = Response::stream(std::move(provider), content_length, status);
  response.set_header("Content-Type", options.content_type);
  response.set_header("Accept-Ranges", "bytes");
  if (has_range) {
    response.set_header("Content-Range", content_range_value(range, representation_size));
  }
  return response;
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
