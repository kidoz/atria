// Per-connection state for the event-driven runtime.
//
// One Connection per accepted socket. The runtime calls on_readable / on_writable when
// the EventLoop reports readiness. When a worker pool is configured, parsed requests are
// submitted to the pool and the connection enters the Dispatching state until the loop
// thread receives the completion via on_dispatch_complete (called only on the loop
// thread).

#pragma once

#include "atria/parser.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/server_config.hpp"
#include "platform/socket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace atria {
class Application;
}

namespace atria::net {

class Connection;

enum class ConnectionState : std::uint8_t {
  Reading,      // accumulating header + body bytes
  Dispatching,  // request submitted to worker pool, awaiting completion
  Writing,      // sending serialized response
  Closing,      // socket should be closed and connection removed
};

// Hook called by Connection when it has a fully-parsed request and a worker pool is
// available. The runtime supplies a closure that captures the connection by shared_ptr,
// schedules the handler on the worker pool, and posts the response back through the
// completion queue. If no hook is provided (i.e. worker pool is disabled), the connection
// runs the handler synchronously on the loop thread.
using DispatchHook = std::function<void(std::shared_ptr<Connection> conn, Request request)>;

class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Clock = std::chrono::steady_clock;

  Connection(platform::SocketHandle socket, Application& app, const ServerConfig& config,
             DispatchHook dispatch_hook);
  ~Connection() = default;
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;

  [[nodiscard]] platform::NativeSocket fd() const noexcept { return socket_.native(); }
  [[nodiscard]] ConnectionState state() const noexcept { return state_; }
  [[nodiscard]] bool is_closing() const noexcept { return state_ == ConnectionState::Closing; }

  [[nodiscard]] bool is_overdue(Clock::time_point now) const noexcept;

  void on_readable();
  void on_writable();

  // Called on the loop thread when a worker thread has finished executing the handler.
  void on_dispatch_complete(Response response);

  void mark_closing() noexcept;

 private:
  enum class StreamMode : std::uint8_t {
    None,         // not a streaming response
    Chunked,      // HTTP/1.1, no Content-Length: emit Transfer-Encoding: chunked frames
    RawCounted,   // Content-Length is known: emit raw bytes, total bound by length
    RawClosing,   // HTTP/1.0, no Content-Length: emit raw bytes, close on EOS
  };

  void try_parse_and_dispatch();
  void prepare_response_headers(Response& response, bool keep_alive, StreamMode mode);
  void start_writing(Response response, bool keep_alive, HttpVersion version);
  void pull_next_chunk();
  void emit_chunk_into_buffer(std::string_view chunk);
  void emit_terminator_into_buffer();
  void finish_write();

  platform::SocketHandle socket_;
  Application& app_;
  const ServerConfig& config_;
  DispatchHook dispatch_hook_;
  ParseLimits limits_;

  std::string read_buffer_;
  std::string write_buffer_;
  std::size_t write_offset_{0};

  ConnectionState state_{ConnectionState::Reading};
  std::size_t request_count_{0};
  bool keep_alive_after_response_{false};
  HttpVersion last_request_version_{HttpVersion::Http11};
  Clock::time_point last_activity_{Clock::now()};
  Clock::duration current_timeout_{};

  // Slowloris guard: when the first byte of a new request arrives, this is set; cleared
  // when the request is fully parsed. While set, the connection is overdue if more time
  // than `request_header_timeout_ms` has passed since the request started.
  std::optional<Clock::time_point> request_started_at_;

  // Streaming-response state. When chunk_provider_ is set, the runtime pulls more bytes
  // each time write_buffer_ drains.
  ChunkProvider chunk_provider_;
  StreamMode stream_mode_{StreamMode::None};
  std::optional<std::size_t> stream_remaining_;  // for RawCounted: bytes left to emit
  bool stream_finished_{false};                   // EOS reached and terminator (if any) queued
};

}  // namespace atria::net
