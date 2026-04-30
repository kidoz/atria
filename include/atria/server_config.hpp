#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace atria {

struct ServerConfig {
  std::string host{"127.0.0.1"};
  std::uint16_t port{8080};
  std::size_t max_request_line_bytes{std::size_t{8} * 1024};
  std::size_t max_header_bytes{std::size_t{16} * 1024};
  std::size_t max_header_count{64};
  std::size_t max_body_bytes{std::size_t{1024} * 1024};
  std::size_t worker_threads{0};
  std::size_t accept_backlog{128};

  // Keep-alive policy
  std::size_t max_requests_per_connection{100};
  std::uint32_t keep_alive_timeout_ms{5000};

  // I/O timeouts
  std::uint32_t read_timeout_ms{30000};
  std::uint32_t write_timeout_ms{30000};

  // Slowloris guard. Once bytes start arriving for a new request, the headers must be
  // fully received within this window. 0 disables the check.
  std::uint32_t request_header_timeout_ms{10000};

  // Connection budgets. Tighten in production; 0 = unlimited.
  std::size_t max_connections{1024};
  std::size_t max_connections_per_ip{64};

  // WebSocket budgets. These apply after an HTTP/1.1 connection has been upgraded.
  std::size_t max_websocket_frame_bytes{std::size_t{64} * 1024};
  std::size_t max_websocket_message_bytes{std::size_t{1024} * 1024};
  std::size_t max_websocket_queue_bytes{std::size_t{1024} * 1024};
  std::uint32_t websocket_idle_timeout_ms{60000};

  // Optional browser Origin policy for WebSocket upgrades.
  // Empty allowlist + require=false preserves current behavior: all origins and missing
  // Origin headers are accepted. With require=true, an empty allowlist requires Origin
  // presence without restricting its value. Values are exact matches; "*" allows any
  // present origin.
  std::vector<std::string> websocket_allowed_origins;
  bool websocket_require_origin{false};

  // When true and worker_threads > 0, WebSocket text/binary callbacks run on the worker
  // pool. Socket I/O and outbound queue mutation still happen on the event-loop thread.
  bool websocket_worker_callbacks{false};
};

}  // namespace atria
