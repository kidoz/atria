#include "atria/parser.hpp"

#include "atria/headers.hpp"
#include "atria/method.hpp"
#include "atria/request.hpp"
#include "atria/status.hpp"
#include "atria/url.hpp"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace atria {

namespace {

[[nodiscard]] HttpError bad_request(std::string message) {
  return HttpError{Status::BadRequest, std::move(message), false};
}

[[nodiscard]] HttpError needs_more() {
  return HttpError{Status::BadRequest, "incomplete", true};
}

[[nodiscard]] std::string trim_ows(std::string_view sv) {
  std::size_t start = 0;
  std::size_t end = sv.size();
  while (start < end && (sv[start] == ' ' || sv[start] == '\t')) {
    ++start;
  }
  while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t')) {
    --end;
  }
  return std::string{sv.substr(start, end - start)};
}

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

[[nodiscard]] int from_hex(unsigned char c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

// Decode a chunked body starting at `view`. Returns either:
//   - success: filled body + bytes consumed (including the final CRLF after the last chunk)
//   - permanent error
//   - "needs more bytes" (incomplete chunk header or chunk data not fully received)
struct ChunkedResult {
  std::string body;
  std::size_t consumed{0};
};

[[nodiscard]] std::expected<ChunkedResult, HttpError> decode_chunked(std::string_view view,
                                                                      std::size_t max_body_bytes) {
  ChunkedResult result;
  result.body.reserve(view.size());
  std::size_t pos = 0;

  while (true) {
    std::size_t line_end = view.find("\r\n", pos);
    if (line_end == std::string_view::npos) {
      return std::unexpected(needs_more());
    }
    std::string_view size_line = view.substr(pos, line_end - pos);
    // Strip optional chunk extensions after ';'.
    std::size_t semi = size_line.find(';');
    if (semi != std::string_view::npos) {
      size_line = size_line.substr(0, semi);
    }
    if (size_line.empty()) {
      return std::unexpected(bad_request("malformed chunk size"));
    }
    std::size_t chunk_size = 0;
    for (char c : size_line) {
      int v = from_hex(static_cast<unsigned char>(c));
      if (v < 0) {
        return std::unexpected(bad_request("malformed chunk size"));
      }
      // overflow check: chunk_size * 16 + v fits in size_t
      if (chunk_size > (std::size_t{0} - 1U) / 16U) {
        return std::unexpected(HttpError{Status::PayloadTooLarge, "chunk too large", false});
      }
      chunk_size = chunk_size * 16U + static_cast<std::size_t>(v);
    }
    pos = line_end + 2;

    if (chunk_size == 0) {
      // Last chunk. Consume optional trailers (skipped) until terminating CRLF.
      while (true) {
        std::size_t trailer_end = view.find("\r\n", pos);
        if (trailer_end == std::string_view::npos) {
          return std::unexpected(needs_more());
        }
        if (trailer_end == pos) {
          // Empty line — end of trailers.
          pos = trailer_end + 2;
          result.consumed = pos;
          return result;
        }
        pos = trailer_end + 2;
      }
    }

    if (result.body.size() + chunk_size > max_body_bytes) {
      return std::unexpected(HttpError{Status::PayloadTooLarge, "body too large", false});
    }
    if (pos + chunk_size + 2 > view.size()) {
      return std::unexpected(needs_more());
    }
    result.body.append(view.data() + pos, chunk_size);
    pos += chunk_size;
    if (view[pos] != '\r' || view[pos + 1] != '\n') {
      return std::unexpected(bad_request("missing CRLF after chunk data"));
    }
    pos += 2;
  }
}

}  // namespace

std::expected<Request, HttpError> parse_request(std::string_view buffer,
                                                const ParseLimits& limits,
                                                std::size_t* consumed) {
  std::size_t header_terminator = buffer.find("\r\n\r\n");
  if (header_terminator == std::string_view::npos) {
    if (buffer.size() > limits.max_request_line_bytes + limits.max_header_bytes) {
      return std::unexpected(
          HttpError{Status::PayloadTooLarge, "header section too large", false});
    }
    return std::unexpected(needs_more());
  }

  std::string_view header_block = buffer.substr(0, header_terminator);
  std::size_t body_start = header_terminator + 4;

  std::size_t request_line_end = header_block.find("\r\n");
  if (request_line_end == std::string_view::npos) {
    return std::unexpected(bad_request("missing request line"));
  }
  if (request_line_end > limits.max_request_line_bytes) {
    return std::unexpected(HttpError{Status::PayloadTooLarge, "request line too long", false});
  }

  std::string_view request_line = header_block.substr(0, request_line_end);
  std::size_t sp1 = request_line.find(' ');
  if (sp1 == std::string_view::npos) {
    return std::unexpected(bad_request("malformed request line"));
  }
  std::size_t sp2 = request_line.find(' ', sp1 + 1);
  if (sp2 == std::string_view::npos) {
    return std::unexpected(bad_request("malformed request line"));
  }
  std::string_view method_text = request_line.substr(0, sp1);
  std::string_view target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
  std::string_view version = request_line.substr(sp2 + 1);

  Method method{};
  if (!parse_method(method_text, method)) {
    return std::unexpected(HttpError{Status::NotImplemented, "unsupported method", false});
  }
  if (version != "HTTP/1.1" && version != "HTTP/1.0") {
    return std::unexpected(bad_request("unsupported HTTP version"));
  }
  if (target.empty() || target.front() != '/') {
    return std::unexpected(bad_request("invalid request target"));
  }

  std::string_view path = target;
  std::string_view query;
  if (auto q = target.find('?'); q != std::string_view::npos) {
    path = target.substr(0, q);
    query = target.substr(q + 1);
  }

  std::string_view headers_view = header_block.substr(request_line_end + 2);
  if (headers_view.size() > limits.max_header_bytes) {
    return std::unexpected(HttpError{Status::PayloadTooLarge, "header section too large", false});
  }

  Headers headers;
  std::size_t header_count = 0;
  std::size_t pos = 0;
  while (pos < headers_view.size()) {
    std::size_t eol = headers_view.find("\r\n", pos);
    if (eol == std::string_view::npos) {
      eol = headers_view.size();
    }
    std::string_view line = headers_view.substr(pos, eol - pos);
    pos = eol + 2;
    if (line.empty()) {
      continue;
    }
    std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) {
      return std::unexpected(bad_request("malformed header line"));
    }
    std::string_view name_view = line.substr(0, colon);
    if (!Headers::valid_name(name_view)) {
      return std::unexpected(bad_request("invalid header name"));
    }
    std::string value = trim_ows(line.substr(colon + 1));
    if (!Headers::valid_value(value)) {
      return std::unexpected(bad_request("invalid header value"));
    }
    headers.append(std::string{name_view}, std::move(value));
    ++header_count;
    if (header_count > limits.max_header_count) {
      return std::unexpected(HttpError{Status::PayloadTooLarge, "too many headers", false});
    }
  }

  // Detect Content-Length and Transfer-Encoding. RFC 7230 forbids both being present.
  auto te_opt = headers.find("Transfer-Encoding");
  auto cl_opt = headers.find("Content-Length");
  bool is_chunked = false;
  if (te_opt.has_value()) {
    if (cl_opt.has_value()) {
      return std::unexpected(
          bad_request("Content-Length and Transfer-Encoding both present"));
    }
    if (!ascii_iequal(*te_opt, "chunked")) {
      return std::unexpected(
          HttpError{Status::NotImplemented, "unsupported transfer-encoding", false});
    }
    is_chunked = true;
  }

  std::string body;
  std::size_t request_size = 0;

  if (is_chunked) {
    std::string_view body_view = buffer.substr(body_start);
    auto decoded = decode_chunked(body_view, limits.max_body_bytes);
    if (!decoded.has_value()) {
      return std::unexpected(decoded.error());
    }
    body = std::move(decoded->body);
    request_size = body_start + decoded->consumed;
  } else {
    std::size_t body_len = 0;
    if (cl_opt.has_value()) {
      std::string_view value = *cl_opt;
      if (value.empty()) {
        return std::unexpected(bad_request("invalid Content-Length"));
      }
      for (char c : value) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
          return std::unexpected(bad_request("invalid Content-Length"));
        }
      }
      auto parse_result = std::from_chars(value.data(), value.data() + value.size(), body_len);
      if (parse_result.ec != std::errc{} || parse_result.ptr != value.data() + value.size()) {
        return std::unexpected(bad_request("invalid Content-Length"));
      }
      if (body_len > limits.max_body_bytes) {
        return std::unexpected(HttpError{Status::PayloadTooLarge, "body too large", false});
      }
    }
    if (buffer.size() < body_start + body_len) {
      return std::unexpected(needs_more());
    }
    body.assign(buffer.substr(body_start, body_len));
    request_size = body_start + body_len;
  }

  if (consumed != nullptr) {
    *consumed = request_size;
  }

  Request request{method, std::string{path}, std::string{query}, std::move(headers),
                  std::move(body)};
  request.set_version(version == "HTTP/1.0" ? HttpVersion::Http10 : HttpVersion::Http11);
  request.set_query_params(parse_query(query));
  return request;
}

}  // namespace atria
