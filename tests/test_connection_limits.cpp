// Tests for ServerConfig connection budgets and the slowloris guard.

#include "atria/application.hpp"
#include "atria/json.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"
#include "platform/socket.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kHost = "127.0.0.1";

[[nodiscard]] std::uint16_t pick_port() {
  auto probe = atria::platform::listen_tcp(kHost, 0, 4);
  if (!probe.has_value()) {
    return 0;
  }
  auto port = atria::platform::local_port(*probe);
  return port.value_or(0);
}

struct RunningServer {
  std::thread thread;
  atria::Application* app{nullptr};
  std::uint16_t port{0};

  ~RunningServer() {
    if (app != nullptr) {
      app->shutdown();
      auto wake = atria::platform::connect_tcp(kHost, port);
      (void)wake;
    }
    if (thread.joinable()) {
      thread.join();
    }
  }
};

// Wait until `connect_tcp` succeeds (server is accepting).
void wait_for_ready(std::uint16_t port) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    auto probe_connection = atria::platform::connect_tcp(kHost, port);
    if (probe_connection.has_value()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
}

}  // namespace

TEST_CASE("server enforces max_connections_per_ip", "[limits]") {
  atria::Application app;
  app.get("/", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"ok", true}}));
  });

  RunningServer server;
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig cfg;
    cfg.host = std::string{kHost};
    cfg.port = server.port;
    cfg.worker_threads = 0;
    cfg.max_connections_per_ip = 3;
    cfg.keep_alive_timeout_ms = 60000;  // keep them open during the test
    app.listen(cfg);
  }};
  server.app = &app;
  wait_for_ready(server.port);

  // Open 3 connections — these should all succeed.
  std::vector<atria::platform::SocketHandle> live_connections;
  for (int connection_index = 0; connection_index < 3; ++connection_index) {
    auto live_connection = atria::platform::connect_tcp(kHost, server.port);
    REQUIRE(live_connection.has_value());
    live_connections.push_back(std::move(*live_connection));
  }
  // Give the server time to register the accepts.
  std::this_thread::sleep_for(std::chrono::milliseconds{50});

  // The 4th connection: from the client side it appears successful (TCP completes),
  // but the server immediately closes it. A subsequent recv returns EOF.
  auto over_limit_connection = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(over_limit_connection.has_value());
  std::array<char, 256> read_buffer{};
  // Wait briefly for the server to close.
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  auto recv_result =
      atria::platform::recv_some(*over_limit_connection, read_buffer.data(), read_buffer.size());
  // Either EOF (bytes==0) or error (peer closed) — both are acceptable.
  CHECK((!recv_result.has_value() || *recv_result == 0));
}

TEST_CASE("server boots slowloris-style clients via header timeout", "[limits][slowloris]") {
  atria::Application app;
  app.get("/", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"ok", true}}));
  });

  RunningServer server;
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig cfg;
    cfg.host = std::string{kHost};
    cfg.port = server.port;
    cfg.worker_threads = 0;
    cfg.read_timeout_ms = 60000;          // very long per-recv timeout
    cfg.request_header_timeout_ms = 200;  // tight header deadline
    app.listen(cfg);
  }};
  server.app = &app;
  wait_for_ready(server.port);

  auto slow_connection = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(slow_connection.has_value());

  // Send a partial request line — never the terminating \r\n\r\n.
  REQUIRE(
      atria::platform::send_all(*slow_connection, std::string_view{"GET / HTTP/1.1\r\nHost: x\r\n"})
          .has_value()
  );

  // Wait past the header deadline + the 200ms sweep cadence.
  std::this_thread::sleep_for(std::chrono::milliseconds{600});

  // The server should have closed the connection by now.
  std::array<char, 256> read_buffer{};
  auto recv_result =
      atria::platform::recv_some(*slow_connection, read_buffer.data(), read_buffer.size());
  CHECK((!recv_result.has_value() || *recv_result == 0));
}
