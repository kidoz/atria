#include "net/connection.hpp"

#include "atria/application.hpp"
#include "atria/parser.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"
#include "atria/websocket.hpp"
#include "net/websocket_protocol.hpp"
#include "platform/socket.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] bool
header_contains_token(const Request& req, std::string_view name, std::string_view token) noexcept {
  auto value = req.header(name);
  if (!value.has_value()) {
    return false;
  }

  std::size_t cursor = 0;
  while (cursor < value->size()) {
    auto comma = value->find(',', cursor);
    if (comma == std::string_view::npos) {
      comma = value->size();
    }
    auto part = value->substr(cursor, comma - cursor);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
      part.remove_prefix(1);
    }
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
      part.remove_suffix(1);
    }
    if (ascii_iequal(part, token)) {
      return true;
    }
    cursor = comma + 1;
  }
  return false;
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

[[nodiscard]] bool is_websocket_upgrade_request(const Request& req) noexcept {
  auto upgrade = req.header("Upgrade");
  return req.method() == Method::Get && req.version() == HttpVersion::Http11 &&
         upgrade.has_value() && ascii_iequal(*upgrade, "websocket") &&
         header_contains_token(req, "Connection", "Upgrade");
}

[[nodiscard]] bool is_websocket_subprotocol_char(unsigned char c) noexcept {
  if (c >= '0' && c <= '9') {
    return true;
  }
  if (c >= 'A' && c <= 'Z') {
    return true;
  }
  if (c >= 'a' && c <= 'z') {
    return true;
  }
  switch (c) {
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '\'':
  case '*':
  case '+':
  case '-':
  case '.':
  case '^':
  case '_':
  case '`':
  case '|':
  case '~':
    return true;
  default:
    return false;
  }
}

[[nodiscard]] bool is_valid_websocket_subprotocol(std::string_view protocol) noexcept {
  if (protocol.empty()) {
    return false;
  }
  return std::ranges::all_of(protocol, [](char c) {
    return is_websocket_subprotocol_char(static_cast<unsigned char>(c));
  });
}

[[nodiscard]] std::optional<std::vector<std::string>>
requested_websocket_subprotocols(const Request& request) {
  auto header = request.header("Sec-WebSocket-Protocol");
  if (!header.has_value()) {
    return std::vector<std::string>{};
  }

  std::vector<std::string> protocols;
  std::size_t cursor = 0;
  while (cursor < header->size()) {
    auto comma = header->find(',', cursor);
    if (comma == std::string_view::npos) {
      comma = header->size();
    }
    auto token = header->substr(cursor, comma - cursor);
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
      token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
      token.remove_suffix(1);
    }
    if (!is_valid_websocket_subprotocol(token)) {
      return std::nullopt;
    }
    protocols.emplace_back(token);
    cursor = comma + 1;
  }
  return protocols;
}

[[nodiscard]] std::optional<WebSocketCloseCode>
close_code_from_payload(std::string_view payload) noexcept {
  if (payload.size() < 2) {
    return WebSocketCloseCode::NormalClosure;
  }
  auto high = static_cast<unsigned char>(payload[0]);
  auto low = static_cast<unsigned char>(payload[1]);
  auto code = static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
  switch (code) {
  case 1000:
    return WebSocketCloseCode::NormalClosure;
  case 1002:
    return WebSocketCloseCode::ProtocolError;
  case 1009:
    return WebSocketCloseCode::MessageTooBig;
  case 1011:
    return WebSocketCloseCode::InternalError;
  default:
    return WebSocketCloseCode::ProtocolError;
  }
}

}  // namespace

Connection::Connection(
    platform::SocketHandle socket,
    Application& app,
    const ServerConfig& config,
    DispatchHook dispatch_hook,
    LoopTaskHook loop_task_hook
)
    : socket_(std::move(socket)),
      app_(app),
      config_(config),
      dispatch_hook_(std::move(dispatch_hook)),
      loop_task_hook_(std::move(loop_task_hook)),
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

void Connection::mark_closing() noexcept {
  state_ = ConnectionState::Closing;
}

bool Connection::wants_write() const noexcept {
  if (state_ == ConnectionState::Writing) {
    if (write_offset_ < write_buffer_.size()) {
      return true;
    }
    return stream_mode_ != StreamMode::None && !stream_finished_ && !stream_waiting_for_wake_;
  }
  if (state_ == ConnectionState::WebSocket) {
    return !write_buffer_.empty() || !websocket_outbox_.empty();
  }
  return false;
}

void Connection::on_readable() {
  if (state_ == ConnectionState::WebSocket) {
    on_websocket_readable();
    return;
  }
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
    const std::size_t cap =
        limits_.max_request_line_bytes + limits_.max_header_bytes + limits_.max_body_bytes + 256;
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

    if (is_websocket_upgrade_request(request)) {
      start_websocket(std::move(request));
      return;
    }

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

void Connection::on_stream_wake() {
  if (state_ != ConnectionState::Writing || stream_mode_ == StreamMode::None || stream_finished_) {
    return;
  }
  stream_waiting_for_wake_ = false;
  if (write_offset_ >= write_buffer_.size()) {
    write_buffer_.clear();
    write_offset_ = 0;
    pull_next_chunk();
  }
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
        std::string{"timeout="} + std::to_string(config_.keep_alive_timeout_ms / 1000U) +
            ", max=" + std::to_string(config_.max_requests_per_connection - request_count_)
    );
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
    wakeable_chunk_provider_ = response.take_wakeable_chunk_provider();
    if (wakeable_chunk_provider_) {
      install_stream_waker();
    }
  } else {
    chunk_provider_ = ChunkProvider{};
    wakeable_chunk_provider_ = WakeableChunkProvider{};
    stream_remaining_.reset();
  }
  stream_mode_ = mode;
  stream_finished_ = false;
  stream_waiting_for_wake_ = false;

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
  if (stream_finished_ || (!chunk_provider_ && !wakeable_chunk_provider_)) {
    return;
  }
  while (true) {
    std::optional<std::string> next;
    if (wakeable_chunk_provider_) {
      next = wakeable_chunk_provider_(stream_waker_);
      if (!next.has_value()) {
        stream_waiting_for_wake_ = true;
        return;
      }
    } else {
      next = chunk_provider_();
      if (!next.has_value()) {
        stream_finished_ = true;
        emit_terminator_into_buffer();
        return;
      }
    }
    stream_waiting_for_wake_ = false;
    if (next->empty()) {
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
  if (state_ == ConnectionState::WebSocket) {
    on_websocket_writable();
    return;
  }
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
  if (upgrade_to_websocket_after_write_) {
    upgrade_to_websocket_after_write_ = false;
    write_buffer_.clear();
    write_offset_ = 0;
    state_ = ConnectionState::WebSocket;
    current_timeout_ = std::chrono::milliseconds{config_.websocket_idle_timeout_ms};
    last_activity_ = Clock::now();
    if (!read_buffer_.empty()) {
      process_websocket_buffer();
    }
    return;
  }
  if (!keep_alive_after_response_ || stream_mode_ == StreamMode::RawClosing) {
    state_ = ConnectionState::Closing;
    return;
  }
  write_buffer_.clear();
  write_offset_ = 0;
  chunk_provider_ = ChunkProvider{};
  wakeable_chunk_provider_ = WakeableChunkProvider{};
  stream_waker_ = StreamWaker{};
  stream_mode_ = StreamMode::None;
  stream_remaining_.reset();
  stream_finished_ = false;
  stream_waiting_for_wake_ = false;
  state_ = ConnectionState::Reading;
  current_timeout_ = std::chrono::milliseconds{config_.keep_alive_timeout_ms};
  last_activity_ = Clock::now();
  if (!read_buffer_.empty()) {
    try_parse_and_dispatch();
  }
}

void Connection::start_websocket(Request request) {
  auto version = request.header("Sec-WebSocket-Version");
  if (!version.has_value() || *version != "13") {
    start_writing(
        error_response(Status::BadRequest, "unsupported websocket version"),
        /*keep_alive=*/false,
        HttpVersion::Http11
    );
    return;
  }
  if (request.header("Sec-WebSocket-Extensions").has_value()) {
    start_writing(
        error_response(Status::BadRequest, "websocket extensions are not supported"),
        /*keep_alive=*/false,
        HttpVersion::Http11
    );
    return;
  }
  auto key = request.header("Sec-WebSocket-Key");
  if (!key.has_value()) {
    start_writing(
        error_response(Status::BadRequest, "missing Sec-WebSocket-Key"),
        /*keep_alive=*/false,
        HttpVersion::Http11
    );
    return;
  }
  auto accept = websocket::make_accept_key(*key);
  if (!accept.has_value()) {
    start_writing(
        error_response(Status::BadRequest, accept.error().message),
        /*keep_alive=*/false,
        HttpVersion::Http11
    );
    return;
  }
  auto requested_subprotocols = requested_websocket_subprotocols(request);
  if (!requested_subprotocols.has_value()) {
    start_writing(
        error_response(Status::BadRequest, "invalid websocket subprotocol"),
        /*keep_alive=*/false,
        HttpVersion::Http11
    );
    return;
  }

  websocket_request_ = std::move(request);
  websocket_session_ = WebSocketSession{};
  websocket_session_.set_request(&*websocket_request_);
  websocket_session_.set_requested_subprotocols(std::move(*requested_subprotocols));
  std::weak_ptr<Connection> weak = shared_from_this();
  auto post = loop_task_hook_;
  auto post_to_connection = [weak, post](ConnectionTask task) {
    if (post) {
      post(weak, std::move(task));
      return;
    }
    if (auto connection = weak.lock()) {
      task(*connection);
    }
  };
  websocket_session_.set_text_sender([post_to_connection](std::string message) {
    post_to_connection([message = std::move(message)](Connection& connection) {
      connection.queue_websocket_frame(websocket::Opcode::Text, message);
    });
  });
  websocket_session_.set_binary_sender([post_to_connection](std::string message) {
    post_to_connection([message = std::move(message)](Connection& connection) {
      connection.queue_websocket_frame(websocket::Opcode::Binary, message);
    });
  });
  websocket_session_.set_close_sender(
      [post_to_connection](WebSocketCloseCode code, std::string reason) {
        post_to_connection([code, reason = std::move(reason)](Connection& connection) {
          connection.queue_websocket_close(code, reason);
        });
      }
  );

  if (!app_.dispatch_websocket(*websocket_request_, websocket_session_)) {
    websocket_request_.reset();
    start_writing(
        error_response(Status::NotFound, "websocket route not found"),
        /*keep_alive=*/false,
        HttpVersion::Http11
    );
    return;
  }

  write_buffer_ = "HTTP/1.1 101 Switching Protocols\r\n";
  write_buffer_.append("Upgrade: websocket\r\n");
  write_buffer_.append("Connection: Upgrade\r\n");
  write_buffer_.append("Sec-WebSocket-Accept: ");
  write_buffer_.append(*accept);
  if (!websocket_session_.selected_subprotocol().empty()) {
    write_buffer_.append("\r\nSec-WebSocket-Protocol: ");
    write_buffer_.append(websocket_session_.selected_subprotocol());
  }
  write_buffer_.append("\r\n\r\n");
  write_offset_ = 0;
  upgrade_to_websocket_after_write_ = true;
  state_ = ConnectionState::Writing;
  current_timeout_ = std::chrono::milliseconds{config_.write_timeout_ms};
  last_activity_ = Clock::now();
}

void Connection::on_websocket_readable() {
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
      state_ = ConnectionState::Closing;
      return;
    }
    read_buffer_.append(chunk.data(), *n);
    last_activity_ = Clock::now();
    process_websocket_buffer();
    if (state_ != ConnectionState::WebSocket) {
      return;
    }
    if (read_buffer_.size() > config_.max_websocket_frame_bytes + 14U) {
      queue_websocket_close(WebSocketCloseCode::MessageTooBig, "websocket frame too large");
      return;
    }
  }
  process_websocket_buffer();
}

void Connection::process_websocket_buffer() {
  while (state_ == ConnectionState::WebSocket && !read_buffer_.empty()) {
    auto parsed = websocket::parse_frame(read_buffer_, config_.max_websocket_frame_bytes);
    if (parsed.status == websocket::ParseStatus::Incomplete) {
      return;
    }
    if (parsed.status == websocket::ParseStatus::ProtocolError ||
        parsed.status == websocket::ParseStatus::MessageTooBig) {
      queue_websocket_close(parsed.close_code, parsed.message);
      return;
    }
    read_buffer_.erase(0, parsed.consumed);
    handle_websocket_frame(parsed.frame);
  }
}

void Connection::handle_websocket_frame(const websocket::Frame& frame) {
  using websocket::Opcode;

  switch (frame.opcode) {
  case Opcode::Text:
  case Opcode::Binary:
    if (websocket_fragment_opcode_.has_value()) {
      queue_websocket_close(WebSocketCloseCode::ProtocolError, "nested websocket fragment");
      return;
    }
    if (frame.fin) {
      if (frame.opcode == Opcode::Text) {
        if (!websocket::is_valid_utf8(frame.payload)) {
          queue_websocket_close(
              WebSocketCloseCode::InvalidFramePayloadData,
              "invalid utf-8 payload"
          );
          return;
        }
        websocket_session_.receive_text(frame.payload);
      } else {
        websocket_session_.receive_binary(frame.payload);
      }
      return;
    }
    websocket_fragment_opcode_ = frame.opcode;
    websocket_message_buffer_ = frame.payload;
    if (websocket_message_buffer_.size() > config_.max_websocket_message_bytes) {
      queue_websocket_close(WebSocketCloseCode::MessageTooBig, "websocket message too large");
    }
    return;
  case Opcode::Continuation:
    if (!websocket_fragment_opcode_.has_value()) {
      queue_websocket_close(WebSocketCloseCode::ProtocolError, "unexpected continuation frame");
      return;
    }
    if (websocket_message_buffer_.size() + frame.payload.size() >
        config_.max_websocket_message_bytes) {
      queue_websocket_close(WebSocketCloseCode::MessageTooBig, "websocket message too large");
      return;
    }
    websocket_message_buffer_.append(frame.payload);
    if (frame.fin) {
      auto opcode = *websocket_fragment_opcode_;
      websocket_fragment_opcode_.reset();
      std::string message = std::move(websocket_message_buffer_);
      websocket_message_buffer_.clear();
      if (opcode == Opcode::Text) {
        if (!websocket::is_valid_utf8(message)) {
          queue_websocket_close(
              WebSocketCloseCode::InvalidFramePayloadData,
              "invalid utf-8 payload"
          );
          return;
        }
        websocket_session_.receive_text(message);
      } else {
        websocket_session_.receive_binary(message);
      }
    }
    return;
  case Opcode::Ping:
    queue_websocket_frame(Opcode::Pong, frame.payload);
    return;
  case Opcode::Pong:
    return;
  case Opcode::Close: {
    auto code = close_code_from_payload(frame.payload).value_or(WebSocketCloseCode::ProtocolError);
    websocket_session_.receive_close(code);
    if (!websocket_close_queued_) {
      queue_websocket_close(code, {});
    }
    websocket_close_after_write_ = true;
    return;
  }
  }
}

void Connection::queue_websocket_frame(websocket::Opcode opcode, std::string_view payload) {
  if (state_ == ConnectionState::Closing || websocket_close_after_write_) {
    return;
  }
  auto encoded = websocket::encode_frame(opcode, payload);
  if (websocket_queued_bytes_ + encoded.size() > config_.max_websocket_queue_bytes) {
    queue_websocket_close(WebSocketCloseCode::MessageTooBig, "websocket queue too large");
    return;
  }
  websocket_queued_bytes_ += encoded.size();
  websocket_outbox_.push(std::move(encoded));
}

void Connection::queue_websocket_close(WebSocketCloseCode code, std::string_view reason) {
  if (websocket_close_queued_) {
    return;
  }
  auto encoded = websocket::encode_close_frame(code, reason);
  websocket_queued_bytes_ += encoded.size();
  websocket_outbox_.push(std::move(encoded));
  websocket_close_queued_ = true;
  websocket_close_after_write_ = true;
}

void Connection::on_websocket_writable() {
  flush_websocket_output();
}

void Connection::flush_websocket_output() {
  while (state_ == ConnectionState::WebSocket) {
    if (write_offset_ >= write_buffer_.size()) {
      if (!write_buffer_.empty()) {
        write_buffer_.clear();
        write_offset_ = 0;
      }
      if (websocket_outbox_.empty()) {
        if (websocket_close_after_write_) {
          state_ = ConnectionState::Closing;
        }
        return;
      }
      write_buffer_ = std::move(websocket_outbox_.front());
      websocket_outbox_.pop();
      websocket_queued_bytes_ -= write_buffer_.size();
      write_offset_ = 0;
    }

    auto sent = platform::send_some(socket_, std::string_view{write_buffer_}.substr(write_offset_));
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
}

void Connection::install_stream_waker() {
  std::weak_ptr<Connection> weak = shared_from_this();
  auto post = loop_task_hook_;
  stream_waker_ = StreamWaker{[weak, post]() {
    auto task = [](Connection& connection) { connection.on_stream_wake(); };
    if (post) {
      post(weak, std::move(task));
      return;
    }
    if (auto connection = weak.lock()) {
      task(*connection);
    }
  }};
}

void Connection::post_loop_task(ConnectionTask task) {
  if (loop_task_hook_) {
    loop_task_hook_(weak_from_this(), std::move(task));
    return;
  }
  task(*this);
}

}  // namespace atria::net
