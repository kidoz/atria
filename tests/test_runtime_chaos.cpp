// Chaos / robustness tests for the event-driven runtime.
//
// Goal: cover failure modes the happy-path integration tests don't reach — peer
// disconnects mid-write, request bytes split across many recv() calls, WebSocket frames
// reassembled byte-by-byte, and accept-storms that pressure the listening backlog.
//
// Determinism notes: tests use ephemeral ports (bind 0 + local_port), short timeouts,
// and bounded retry loops. They don't measure throughput; they only assert that the
// runtime survives the scenario and produces correct results where applicable.

#include "atria/application.hpp"
#include "atria/json.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"
#include "atria/websocket.hpp"
#include "platform/socket.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
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

void wait_until_ready(std::uint16_t port) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    auto probe = atria::platform::connect_tcp(kHost, port);
    if (probe.has_value()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
}

[[nodiscard]] std::string websocket_handshake(std::string_view path = "/ws") {
  std::string request = "GET ";
  request.append(path);
  request.append(" HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n");
  request.append("Connection: keep-alive, Upgrade\r\n");
  request.append("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
  request.append("Sec-WebSocket-Version: 13\r\n\r\n");
  return request;
}

[[nodiscard]] std::string masked_text_frame(std::string_view payload) {
  std::string frame;
  // FIN | opcode=0x1 (text)
  frame.push_back(static_cast<char>(0x81));
  // mask bit set + length
  if (payload.size() <= 125) {
    frame.push_back(static_cast<char>(0x80U | static_cast<std::uint8_t>(payload.size())));
  } else {
    REQUIRE(payload.size() <= 0xFFFFU);
    frame.push_back(static_cast<char>(0x80U | 126U));
    frame.push_back(static_cast<char>((payload.size() >> 8U) & 0xFFU));
    frame.push_back(static_cast<char>(payload.size() & 0xFFU));
  }
  // Constant mask key 0xAA, 0xBB, 0xCC, 0xDD; XOR the payload.
  const std::array<std::uint8_t, 4> mask = {0xAA, 0xBB, 0xCC, 0xDD};
  for (auto byte : mask) {
    frame.push_back(static_cast<char>(byte));
  }
  for (std::size_t index = 0; index < payload.size(); ++index) {
    frame.push_back(
        static_cast<char>(static_cast<std::uint8_t>(payload[index]) ^ mask[index % 4U]));
  }
  return frame;
}

[[nodiscard]] std::string read_until(atria::platform::SocketHandle& connection,
                                       std::string_view marker) {
  std::string accumulated;
  std::array<char, 1024> buffer{};
  while (accumulated.find(marker) == std::string::npos) {
    auto received = atria::platform::recv_some(connection, buffer.data(), buffer.size());
    if (!received.has_value() || *received == 0) {
      break;
    }
    accumulated.append(buffer.data(), *received);
  }
  return accumulated;
}

}  // namespace

TEST_CASE("server survives peer disconnect mid-write", "[chaos]") {
  // Build a streaming response large enough that the kernel send buffer cannot drain
  // it in a single non-blocking write. The test connects, asks for the response, then
  // closes the socket immediately. The server must clean up without crashing.
  atria::Application app;
  app.get("/big", [](atria::Request&) {
    auto remaining = std::make_shared<std::size_t>(8U * 1024U * 1024U);  // 8 MiB
    return atria::Response::stream([remaining]() -> std::optional<std::string> {
      if (*remaining == 0) {
        return std::nullopt;
      }
      std::size_t chunk_size = std::min<std::size_t>(*remaining, 64U * 1024U);
      *remaining -= chunk_size;
      return std::string(chunk_size, 'x');
    });
  });

  RunningServer server;
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig config;
    config.host = std::string{kHost};
    config.port = server.port;
    config.worker_threads = 0;
    app.listen(config);
  }};
  server.app = &app;
  wait_until_ready(server.port);

  // Connect, send the request, read a tiny prefix, then drop the socket.
  {
    auto client_connection = atria::platform::connect_tcp(kHost, server.port);
    REQUIRE(client_connection.has_value());
    REQUIRE(atria::platform::send_all(*client_connection,
                                       std::string_view{"GET /big HTTP/1.1\r\n"
                                                        "Host: 127.0.0.1\r\n"
                                                        "Connection: close\r\n"
                                                        "\r\n"})
                .has_value());
    std::array<char, 1024> read_buffer{};
    auto first_read =
        atria::platform::recv_some(*client_connection, read_buffer.data(), read_buffer.size());
    REQUIRE(first_read.has_value());
    REQUIRE(*first_read > 0);
    // client_connection goes out of scope here — RAII closes the socket mid-stream.
  }

  // The server must still serve other requests afterwards. If the disconnect-mid-write
  // path leaks state, this second request would hang or fail.
  auto follow_up = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(follow_up.has_value());
  REQUIRE(atria::platform::send_all(*follow_up,
                                     std::string_view{"GET /big HTTP/1.1\r\n"
                                                      "Host: 127.0.0.1\r\n"
                                                      "Connection: close\r\n"
                                                      "\r\n"})
              .has_value());
  std::array<char, 1024> follow_up_buffer{};
  auto follow_up_first =
      atria::platform::recv_some(*follow_up, follow_up_buffer.data(), follow_up_buffer.size());
  REQUIRE(follow_up_first.has_value());
  CHECK(*follow_up_first > 0);
}

TEST_CASE("server reassembles a WebSocket frame sent byte-by-byte", "[chaos][websocket]") {
  std::atomic<int> messages_received{0};
  std::string captured_message;
  std::mutex captured_mutex;

  atria::Application app;
  app.websocket("/ws", [&](atria::WebSocketSession& session) {
    session.on_text([&](atria::WebSocketSession&, std::string_view text) {
      {
        std::lock_guard<std::mutex> lock(captured_mutex);
        captured_message.assign(text);
      }
      ++messages_received;
    });
  });

  RunningServer server;
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig config;
    config.host = std::string{kHost};
    config.port = server.port;
    config.worker_threads = 0;
    app.listen(config);
  }};
  server.app = &app;
  wait_until_ready(server.port);

  auto connection = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(connection.has_value());
  REQUIRE(atria::platform::send_all(*connection, websocket_handshake()).has_value());
  std::string handshake_response = read_until(*connection, "\r\n\r\n");
  REQUIRE(handshake_response.find("HTTP/1.1 101") == 0);

  // Send a 50-byte text frame one byte at a time. The server's WebSocket parser must
  // accumulate across many recv() calls and dispatch a single on_text() callback.
  const std::string payload = "the quick brown fox jumps over the lazy dog 12345";
  REQUIRE(payload.size() == 49U);
  std::string frame_bytes = masked_text_frame(payload);
  for (char byte : frame_bytes) {
    REQUIRE(atria::platform::send_all(*connection, std::string_view{&byte, 1}).has_value());
    std::this_thread::sleep_for(std::chrono::microseconds{200});
  }

  // Spin until the server reports the message has been received (capped to keep CI sane).
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (messages_received.load() > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  CHECK(messages_received.load() == 1);
  std::lock_guard<std::mutex> lock(captured_mutex);
  CHECK(captured_message == payload);
}

TEST_CASE("server handles an accept storm without dropping connections", "[chaos]") {
  std::atomic<int> handler_invocations{0};
  atria::Application app;
  app.get("/", [&](atria::Request&) {
    ++handler_invocations;
    return atria::Response::json(atria::Json::object({{"ok", true}}));
  });

  RunningServer server;
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig config;
    config.host = std::string{kHost};
    config.port = server.port;
    // 4 worker threads so handlers run in parallel — closer to a real server.
    config.worker_threads = 4;
    config.accept_backlog = 256;
    config.max_connections = 1024;
    config.max_connections_per_ip = 1024;
    app.listen(config);
  }};
  server.app = &app;
  wait_until_ready(server.port);

  // Fire 60 concurrent connections, each performs a single GET / and reads the body.
  constexpr int kConcurrentRequests = 60;
  auto fire_one_request = [port = server.port]() -> bool {
    auto client_connection = atria::platform::connect_tcp(kHost, port);
    if (!client_connection.has_value()) {
      return false;
    }
    auto sent = atria::platform::send_all(*client_connection,
                                           std::string_view{"GET / HTTP/1.1\r\n"
                                                            "Host: 127.0.0.1\r\n"
                                                            "Connection: close\r\n"
                                                            "\r\n"});
    if (!sent.has_value()) {
      return false;
    }
    std::string response;
    std::array<char, 1024> buffer{};
    while (true) {
      auto received =
          atria::platform::recv_some(*client_connection, buffer.data(), buffer.size());
      if (!received.has_value() || *received == 0) {
        break;
      }
      response.append(buffer.data(), *received);
    }
    return response.find("HTTP/1.1 200 OK\r\n") == 0;
  };

  std::vector<std::future<bool>> outcomes;
  outcomes.reserve(kConcurrentRequests);
  for (int request_index = 0; request_index < kConcurrentRequests; ++request_index) {
    outcomes.emplace_back(std::async(std::launch::async, fire_one_request));
  }

  int successes = 0;
  for (auto& outcome : outcomes) {
    if (outcome.get()) {
      ++successes;
    }
  }
  CHECK(successes == kConcurrentRequests);
  CHECK(handler_invocations.load() == kConcurrentRequests);
}
