#include "net/connection.hpp"

#include "atria/application.hpp"
#include "atria/parser.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"
#include "platform/socket.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace atria::net {

namespace {

[[nodiscard]] Response error_response(Status status, std::string_view message) {
  Response r{status};
  r.set_header("Content-Type", "application/json; charset=utf-8");
  std::string body = R"({"error":{"code":")";
  switch (status) {
    case Status::BadRequest:
      body.append("bad_request");
      break;
    case Status::PayloadTooLarge:
      body.append("payload_too_large");
      break;
    case Status::NotImplemented:
      body.append("not_implemented");
      break;
    default:
      body.append("error");
      break;
  }
  body.append(R"(","message":")");
  for (char c : message) {
    if (c == '"' || c == '\\') {
      body.push_back('\\');
    }
    if (c == '\r' || c == '\n') {
      continue;
    }
    body.push_back(c);
  }
  body.append("\"}}");
  r.set_body(std::move(body));
  return r;
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

[[nodiscard]] bool client_wants_keep_alive(const Request& req) noexcept {
  auto conn = req.header("Connection");
  if (conn.has_value() && ascii_iequal(*conn, "close")) {
    return false;
  }
  if (req.version() == HttpVersion::Http11) {
    return true;
  }
  return conn.has_value() && ascii_iequal(*conn, "keep-alive");
}

}  // namespace

Connection::Connection(platform::SocketHandle socket, Application& app,
                        const ServerConfig& config, DispatchHook dispatch_hook)
    : socket_(std::move(socket)),
      app_(app),
      config_(config),
      dispatch_hook_(std::move(dispatch_hook)),
      limits_(parse_limits_from(config)),
      current_timeout_(std::chrono::milliseconds{config.read_timeout_ms}) {}

bool Connection::is_overdue(Clock::time_point now) const noexcept {
  // Per-recv timeout (resets on each chunk of bytes received or sent).
  if (current_timeout_ != Clock::duration::zero() && now - last_activity_ > current_timeout_) {
    return true;
  }
  // Header-stage absolute deadline: defends against slowloris (1 byte just before each
  // recv timeout). Only applies while a request is in flight but not yet parsed.
  if (request_started_at_.has_value() && config_.request_header_timeout_ms != 0) {
    auto budget = std::chrono::milliseconds{config_.request_header_timeout_ms};
    if (now - *request_started_at_ > budget) {
      return true;
    }
  }
  return false;
}

void Connection::mark_closing() noexcept { state_ = ConnectionState::Closing; }

void Connection::on_readable() {
  if (state_ != ConnectionState::Reading) {
    return;
  }
  std::array<char, 4096> chunk{};
  while (true) {
    auto n = platform::recv_some(socket_, chunk.data(), chunk.size());
    if (!n.has_value()) {
      if (n.error().kind == platform::SocketErrorKind::WouldBlock) {
        break;
      }
      state_ = ConnectionState::Closing;
      return;
    }
    if (*n == 0) {
      if (read_buffer_.empty()) {
        state_ = ConnectionState::Closing;
        return;
      }
      try_parse_and_dispatch();
      if (state_ == ConnectionState::Reading) {
        state_ = ConnectionState::Closing;
      }
      return;
    }
    last_activity_ = Clock::now();
    if (!request_started_at_.has_value()) {
      request_started_at_ = last_activity_;
    }
    const std::size_t cap = limits_.max_request_line_bytes + limits_.max_header_bytes +
                            limits_.max_body_bytes + 256;
    if (read_buffer_.size() + *n > cap) {
      state_ = ConnectionState::Closing;
      return;
    }
    read_buffer_.append(chunk.data(), *n);
  }
  try_parse_and_dispatch();
}

void Connection::try_parse_and_dispatch() {
  while (state_ == ConnectionState::Reading) {
    std::size_t consumed = 0;
    auto parsed = parse_request(read_buffer_, limits_, &consumed);
    if (!parsed.has_value()) {
      if (parsed.error().incomplete) {
        return;
      }
      Response err = error_response(parsed.error().status, parsed.error().message);
      start_writing(std::move(err), /*keep_alive=*/false, HttpVersion::Http11);
      return;
    }

    Request request = std::move(*parsed);
    read_buffer_.erase(0, consumed);
    request_started_at_.reset();
    ++request_count_;

    bool wants_keep = client_wants_keep_alive(request);
    bool reached_max = request_count_ >= config_.max_requests_per_connection;
    keep_alive_after_response_ = wants_keep && !reached_max;
    last_request_version_ = request.version();

    if (dispatch_hook_) {
      state_ = ConnectionState::Dispatching;
      current_timeout_ = std::chrono::milliseconds{config_.read_timeout_ms};
      last_activity_ = Clock::now();
      dispatch_hook_(shared_from_this(), std::move(request));
      return;
    }

    Response response = app_.dispatch(request);
    start_writing(std::move(response), keep_alive_after_response_, last_request_version_);
    return;
  }
}

void Connection::on_dispatch_complete(Response response) {
  if (state_ != ConnectionState::Dispatching) {
    return;
  }
  start_writing(std::move(response), keep_alive_after_response_, last_request_version_);
}

void Connection::prepare_response_headers(Response& response, bool keep_alive, StreamMode mode) {
  if (mode != StreamMode::None) {
    // Strip any user-set framing headers; the runtime owns framing decisions.
    Headers fresh;
    for (const auto& [k, v] : response.headers().entries()) {
      if (ascii_iequal(k, "Content-Length") || ascii_iequal(k, "Transfer-Encoding")) {
        continue;
      }
      fresh.append(k, v);
    }
    response.headers() = std::move(fresh);
    if (mode == StreamMode::Chunked) {
      response.set_header("Transfer-Encoding", "chunked");
    } else if (mode == StreamMode::RawCounted) {
      response.set_header("Content-Length", std::to_string(*stream_remaining_));
    }
    // RawClosing emits no length header; close after.
  }

  bool effective_keep = keep_alive && mode != StreamMode::RawClosing;
  response.set_header("Connection", effective_keep ? "keep-alive" : "close");
  if (effective_keep) {
    response.set_header(
        "Keep-Alive",
        std::string{"timeout="} +
            std::to_string(config_.keep_alive_timeout_ms / 1000U) +
            ", max=" +
            std::to_string(config_.max_requests_per_connection - request_count_));
  }
  keep_alive_after_response_ = effective_keep;
}

void Connection::start_writing(Response response, bool keep_alive, HttpVersion version) {
  StreamMode mode = StreamMode::None;
  if (response.is_streaming()) {
    if (response.content_length().has_value()) {
      mode = StreamMode::RawCounted;
      stream_remaining_ = response.content_length();
    } else if (version == HttpVersion::Http11) {
      mode = StreamMode::Chunked;
    } else {
      mode = StreamMode::RawClosing;
    }
    chunk_provider_ = response.take_chunk_provider();
  } else {
    chunk_provider_ = ChunkProvider{};
    stream_remaining_.reset();
  }
  stream_mode_ = mode;
  stream_finished_ = false;

  prepare_response_headers(response, keep_alive, mode);

  if (mode == StreamMode::None) {
    write_buffer_ = response.serialize();
  } else {
    write_buffer_ = response.serialize_headers();
  }
  write_offset_ = 0;
  state_ = ConnectionState::Writing;
  current_timeout_ = std::chrono::milliseconds{config_.write_timeout_ms};
  last_activity_ = Clock::now();
}

void Connection::emit_chunk_into_buffer(std::string_view chunk) {
  switch (stream_mode_) {
    case StreamMode::Chunked: {
      char hex[32] = {0};
      int n = std::snprintf(hex, sizeof(hex), "%zx", chunk.size());
      if (n < 0) {
        n = 0;
      }
      write_buffer_.append(hex, static_cast<std::size_t>(n));
      write_buffer_.append("\r\n");
      write_buffer_.append(chunk);
      write_buffer_.append("\r\n");
      break;
    }
    case StreamMode::RawCounted: {
      std::size_t emit = chunk.size();
      if (stream_remaining_.has_value() && emit > *stream_remaining_) {
        emit = *stream_remaining_;
      }
      write_buffer_.append(chunk.data(), emit);
      if (stream_remaining_.has_value()) {
        *stream_remaining_ -= emit;
        if (*stream_remaining_ == 0) {
          stream_finished_ = true;
        }
      }
      break;
    }
    case StreamMode::RawClosing:
      write_buffer_.append(chunk);
      break;
    case StreamMode::None:
      break;
  }
}

void Connection::emit_terminator_into_buffer() {
  if (stream_mode_ == StreamMode::Chunked) {
    write_buffer_.append("0\r\n\r\n");
  }
}

void Connection::pull_next_chunk() {
  if (stream_finished_ || !chunk_provider_) {
    return;
  }
  while (true) {
    std::optional<std::string> next = chunk_provider_();
    if (!next.has_value() || next->empty()) {
      stream_finished_ = true;
      emit_terminator_into_buffer();
      return;
    }
    emit_chunk_into_buffer(*next);
    if (stream_finished_) {
      return;
    }
    if (write_buffer_.size() > write_offset_) {
      return;
    }
  }
}

void Connection::on_writable() {
  if (state_ != ConnectionState::Writing) {
    return;
  }
  while (true) {
    while (write_offset_ < write_buffer_.size()) {
      auto sent =
          platform::send_some(socket_, std::string_view{write_buffer_}.substr(write_offset_));
      if (!sent.has_value()) {
        if (sent.error().kind == platform::SocketErrorKind::WouldBlock) {
          return;
        }
        state_ = ConnectionState::Closing;
        return;
      }
      if (*sent == 0) {
        state_ = ConnectionState::Closing;
        return;
      }
      write_offset_ += *sent;
      last_activity_ = Clock::now();
    }
    // Buffer drained.
    if (stream_mode_ != StreamMode::None && !stream_finished_) {
      write_buffer_.clear();
      write_offset_ = 0;
      pull_next_chunk();
      if (write_buffer_.empty()) {
        // Either finished without a trailing terminator (raw modes) or producer
        // returned nothing — loop exits and we fall through to finish_write().
        if (stream_finished_) {
          break;
        }
        return;
      }
      continue;
    }
    break;
  }
  finish_write();
}

void Connection::finish_write() {
  if (!keep_alive_after_response_ || stream_mode_ == StreamMode::RawClosing) {
    state_ = ConnectionState::Closing;
    return;
  }
  write_buffer_.clear();
  write_offset_ = 0;
  chunk_provider_ = ChunkProvider{};
  stream_mode_ = StreamMode::None;
  stream_remaining_.reset();
  stream_finished_ = false;
  state_ = ConnectionState::Reading;
  current_timeout_ = std::chrono::milliseconds{config_.keep_alive_timeout_ms};
  last_activity_ = Clock::now();
  if (!read_buffer_.empty()) {
    try_parse_and_dispatch();
  }
}

}  // namespace atria::net
