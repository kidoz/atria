#include "atria/application.hpp"
#include "atria/server_config.hpp"
#include "atria/websocket.hpp"
#include "platform/socket.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view kHost = "127.0.0.1";

[[nodiscard]] std::uint16_t pick_port() {
  for (std::uint16_t candidate = 19100; candidate < 19180; ++candidate) {
    auto probe = atria::platform::listen_tcp(kHost, candidate, 4);
    if (probe.has_value()) {
      return candidate;
    }
  }
  return 0;
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
  for (int i = 0; i < 100; ++i) {
    auto conn = atria::platform::connect_tcp(kHost, port);
    if (conn.has_value()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
}

[[nodiscard]] std::string read_until(atria::platform::SocketHandle& conn, std::string_view marker) {
  std::string out;
  std::array<char, 1024> buffer{};
  while (out.find(marker) == std::string::npos) {
    auto n = atria::platform::recv_some(conn, buffer.data(), buffer.size());
    if (!n.has_value() || *n == 0) {
      break;
    }
    out.append(buffer.data(), *n);
  }
  return out;
}

[[nodiscard]] std::string read_all(atria::platform::SocketHandle& conn) {
  std::string out;
  std::array<char, 1024> buffer{};
  while (true) {
    auto n = atria::platform::recv_some(conn, buffer.data(), buffer.size());
    if (!n.has_value() || *n == 0) {
      break;
    }
    out.append(buffer.data(), *n);
  }
  return out;
}

[[nodiscard]] std::string websocket_handshake(std::string_view path = "/ws") {
  std::string request = "GET ";
  request.append(path);
  request.append(" HTTP/1.1\r\n");
  request.append("Host: 127.0.0.1\r\n");
  request.append("Upgrade: websocket\r\n");
  request.append("Connection: keep-alive, Upgrade\r\n");
  request.append("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
  request.append("Sec-WebSocket-Version: 13\r\n\r\n");
  return request;
}

[[nodiscard]] std::string masked_client_frame(std::uint8_t opcode, std::string_view payload) {
  std::string frame;
  frame.push_back(static_cast<char>(0x80U | opcode));
  frame.push_back(static_cast<char>(0x80U | static_cast<std::uint8_t>(payload.size())));
  std::array<unsigned char, 4> mask{0x11, 0x22, 0x33, 0x44};
  for (auto byte : mask) {
    frame.push_back(static_cast<char>(byte));
  }
  for (std::size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]));
  }
  return frame;
}

struct ServerFrame {
  std::uint8_t opcode{0};
  std::string payload;
};

[[nodiscard]] ServerFrame read_server_frame(atria::platform::SocketHandle& conn) {
  std::string bytes;
  std::array<char, 1024> buffer{};
  while (bytes.size() < 2) {
    auto n = atria::platform::recv_some(conn, buffer.data(), buffer.size());
    REQUIRE(n.has_value());
    REQUIRE(*n != 0);
    bytes.append(buffer.data(), *n);
  }
  auto first = static_cast<unsigned char>(bytes[0]);
  auto second = static_cast<unsigned char>(bytes[1]);
  std::size_t offset = 2;
  std::size_t payload_len = second & 0x7FU;
  if (payload_len == 126) {
    while (bytes.size() < 4) {
      auto n = atria::platform::recv_some(conn, buffer.data(), buffer.size());
      REQUIRE(n.has_value());
      bytes.append(buffer.data(), *n);
    }
    payload_len = (static_cast<std::size_t>(static_cast<unsigned char>(bytes[2])) << 8U) |
                  static_cast<unsigned char>(bytes[3]);
    offset = 4;
  }
  while (bytes.size() < offset + payload_len) {
    auto n = atria::platform::recv_some(conn, buffer.data(), buffer.size());
    REQUIRE(n.has_value());
    REQUIRE(*n != 0);
    bytes.append(buffer.data(), *n);
  }
  return ServerFrame{static_cast<std::uint8_t>(first & 0x0FU), bytes.substr(offset, payload_len)};
}

void start_server(RunningServer& server, atria::Application& app) {
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig cfg;
    cfg.host = std::string{kHost};
    cfg.port = server.port;
    cfg.worker_threads = 0;
    app.listen(cfg);
  }};
  server.app = &app;
  wait_until_ready(server.port);
}

}  // namespace

TEST_CASE("websocket handshake returns switching protocols", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(atria::platform::send_all(*conn, websocket_handshake()).has_value());

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
  CHECK(response.find("Upgrade: websocket\r\n") != std::string::npos);
  CHECK(response.find("Connection: Upgrade\r\n") != std::string::npos);
  CHECK(
      response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos
  );
}

TEST_CASE("websocket route echoes text frames", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession& session) {
    session.on_text([](atria::WebSocketSession& ws, std::string_view message) {
      std::string reply = "echo:";
      reply.append(message);
      ws.send_text(std::move(reply));
    });
  });

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(atria::platform::send_all(*conn, websocket_handshake()).has_value());
  auto response = read_until(*conn, "\r\n\r\n");
  REQUIRE(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);

  REQUIRE(atria::platform::send_all(*conn, masked_client_frame(0x1, "hello")).has_value());
  auto frame = read_server_frame(*conn);
  CHECK(frame.opcode == 0x1);
  CHECK(frame.payload == "echo:hello");
}

TEST_CASE("websocket rejects unsupported extensions", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());

  std::string request = websocket_handshake();
  request.insert(request.find("\r\n\r\n"), "Sec-WebSocket-Extensions: permessage-deflate\r\n");
  REQUIRE(atria::platform::send_all(*conn, request).has_value());

  std::string response = read_all(*conn);
  CHECK(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
}
