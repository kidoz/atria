#include "atria/application.hpp"
#include "atria/server_config.hpp"
#include "atria/websocket.hpp"
#include "platform/socket.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

[[nodiscard]] std::string websocket_handshake(
    std::string_view path = "/ws",
    std::string_view protocols = {},
    std::string_view origin = {}
) {
  std::string request = "GET ";
  request.append(path);
  request.append(" HTTP/1.1\r\n");
  request.append("Host: 127.0.0.1\r\n");
  if (!origin.empty()) {
    request.append("Origin: ");
    request.append(origin);
    request.append("\r\n");
  }
  request.append("Upgrade: websocket\r\n");
  request.append("Connection: keep-alive, Upgrade\r\n");
  request.append("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
  request.append("Sec-WebSocket-Version: 13\r\n");
  if (!protocols.empty()) {
    request.append("Sec-WebSocket-Protocol: ");
    request.append(protocols);
    request.append("\r\n");
  }
  request.append("\r\n");
  return request;
}

[[nodiscard]] std::string
client_frame(std::uint8_t opcode, std::string_view payload, bool masked = true, bool fin = true) {
  std::string frame;
  frame.push_back(static_cast<char>((fin ? 0x80U : 0U) | opcode));
  auto mask_bit = masked ? 0x80U : 0U;
  if (payload.size() <= 125) {
    frame.push_back(static_cast<char>(mask_bit | static_cast<std::uint8_t>(payload.size())));
  } else if (payload.size() <= 0xFFFFU) {
    frame.push_back(static_cast<char>(mask_bit | 126U));
    frame.push_back(static_cast<char>((payload.size() >> 8U) & 0xFFU));
    frame.push_back(static_cast<char>(payload.size() & 0xFFU));
  } else {
    frame.push_back(static_cast<char>(mask_bit | 127U));
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<char>((payload.size() >> static_cast<unsigned>(shift)) & 0xFFU));
    }
  }
  if (!masked) {
    frame.append(payload);
    return frame;
  }
  std::array<unsigned char, 4> mask{0x11, 0x22, 0x33, 0x44};
  for (auto byte : mask) {
    frame.push_back(static_cast<char>(byte));
  }
  for (std::size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]));
  }
  return frame;
}

[[nodiscard]] std::string masked_client_frame(std::uint8_t opcode, std::string_view payload) {
  return client_frame(opcode, payload);
}

[[nodiscard]] std::string close_payload(std::uint16_t code) {
  std::string payload;
  payload.push_back(static_cast<char>((code >> 8U) & 0xFFU));
  payload.push_back(static_cast<char>(code & 0xFFU));
  return payload;
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

void start_server(
    RunningServer& server,
    atria::Application& app,
    const std::function<void(atria::ServerConfig&)>& configure = {}
) {
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig cfg;
    cfg.host = std::string{kHost};
    cfg.port = server.port;
    cfg.worker_threads = 0;
    if (configure) {
      configure(cfg);
    }
    app.listen(cfg);
  }};
  server.app = &app;
  wait_until_ready(server.port);
}

[[nodiscard]] atria::platform::SocketHandle
open_websocket(std::uint16_t port, std::string_view path = "/ws") {
  auto conn = atria::platform::connect_tcp(kHost, port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(atria::platform::send_all(*conn, websocket_handshake(path)).has_value());
  auto response = read_until(*conn, "\r\n\r\n");
  REQUIRE(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
  return std::move(*conn);
}

[[nodiscard]] std::uint16_t close_code(const ServerFrame& frame) {
  REQUIRE(frame.opcode == 0x8);
  REQUIRE(frame.payload.size() >= 2);
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(static_cast<unsigned char>(frame.payload[0])) << 8U) |
      static_cast<unsigned char>(frame.payload[1])
  );
}

[[nodiscard]] std::size_t current_thread_hash() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
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
  CHECK(response.find("Sec-WebSocket-Protocol:") == std::string::npos);
}

TEST_CASE("websocket handler can select a requested subprotocol", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession& session) {
    CHECK(session.requested_subprotocols() == std::vector<std::string>{"chat.v1", "chat.v2"});
    CHECK(session.select_subprotocol("chat.v2"));
    CHECK(session.selected_subprotocol() == "chat.v2");
  });

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(*conn, websocket_handshake("/ws", "chat.v1, chat.v2")).has_value()
  );

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
  CHECK(response.find("Sec-WebSocket-Protocol: chat.v2\r\n") != std::string::npos);
}

TEST_CASE(
    "websocket subprotocol selection preserves client preference when handler does",
    "[websocket]"
) {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession& session) {
    for (auto protocol : session.requested_subprotocols()) {
      if (protocol == "chat.v1" || protocol == "chat.v2") {
        REQUIRE(session.select_subprotocol(protocol));
        return;
      }
    }
  });

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(*conn, websocket_handshake("/ws", "chat.v2, chat.v1")).has_value()
  );

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
  CHECK(response.find("Sec-WebSocket-Protocol: chat.v2\r\n") != std::string::npos);
}

TEST_CASE("websocket unsupported subprotocol is ignored when not selected", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession& session) {
    CHECK_FALSE(session.select_subprotocol("chat.v2"));
  });

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(atria::platform::send_all(*conn, websocket_handshake("/ws", "chat.v1")).has_value());

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
  CHECK(response.find("Sec-WebSocket-Protocol:") == std::string::npos);
}

TEST_CASE("websocket rejects invalid subprotocol header", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(*conn, websocket_handshake("/ws", "chat.v1, bad value")).has_value()
  );

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
  CHECK(response.find("invalid websocket subprotocol") != std::string::npos);
}

TEST_CASE("websocket allows missing origin by default", "[websocket]") {
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
}

TEST_CASE("websocket allows configured origin", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) {
    cfg.websocket_allowed_origins = {"https://app.example"};
  });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(*conn, websocket_handshake("/ws", {}, "https://app.example"))
          .has_value()
  );

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
}

TEST_CASE("websocket rejects disallowed origin", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) {
    cfg.websocket_allowed_origins = {"https://app.example"};
  });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(*conn, websocket_handshake("/ws", {}, "https://evil.example"))
          .has_value()
  );

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 403 Forbidden\r\n") == 0);
  CHECK(response.find("websocket origin is not allowed") != std::string::npos);
}

TEST_CASE("websocket can require origin", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) { cfg.websocket_require_origin = true; });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(atria::platform::send_all(*conn, websocket_handshake()).has_value());

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 403 Forbidden\r\n") == 0);
}

TEST_CASE("websocket require origin accepts any present origin without allowlist", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) { cfg.websocket_require_origin = true; });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(*conn, websocket_handshake("/ws", {}, "https://any.example"))
          .has_value()
  );

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
}

TEST_CASE("websocket wildcard origin allows any present origin", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) {
    cfg.websocket_allowed_origins = {"*"};
    cfg.websocket_require_origin = true;
  });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(*conn, websocket_handshake("/ws", {}, "https://any.example"))
          .has_value()
  );

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
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

TEST_CASE("websocket callbacks stay on loop thread by default with worker pool", "[websocket]") {
  atria::Application app;
  std::atomic<std::size_t> loop_thread_hash{0};
  std::atomic<bool> callback_on_loop{false};

  app.websocket("/ws", [&](atria::WebSocketSession& session) {
    session.on_text([&](atria::WebSocketSession& ws, std::string_view) {
      callback_on_loop.store(
          current_thread_hash() == loop_thread_hash.load(),
          std::memory_order_release
      );
      ws.send_text("loop");
    });
  });

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) { cfg.worker_threads = 1; });
  loop_thread_hash.store(std::hash<std::thread::id>{}(server.thread.get_id()));

  auto conn = open_websocket(server.port);
  REQUIRE(atria::platform::send_all(conn, masked_client_frame(0x1, "hello")).has_value());
  auto frame = read_server_frame(conn);

  CHECK(frame.opcode == 0x1);
  CHECK(frame.payload == "loop");
  CHECK(callback_on_loop.load(std::memory_order_acquire));
}

TEST_CASE("websocket text callbacks can run on worker pool", "[websocket]") {
  atria::Application app;
  std::atomic<std::size_t> loop_thread_hash{0};
  std::atomic<bool> callback_on_worker{false};

  app.websocket("/ws", [&](atria::WebSocketSession& session) {
    session.on_text([&](atria::WebSocketSession& ws, std::string_view message) {
      callback_on_worker.store(
          current_thread_hash() != loop_thread_hash.load(),
          std::memory_order_release
      );
      std::string reply = "worker:";
      reply.append(message);
      ws.send_text(std::move(reply));
    });
  });

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) {
    cfg.worker_threads = 1;
    cfg.websocket_worker_callbacks = true;
  });
  loop_thread_hash.store(std::hash<std::thread::id>{}(server.thread.get_id()));

  auto conn = open_websocket(server.port);
  REQUIRE(atria::platform::send_all(conn, masked_client_frame(0x1, "hello")).has_value());
  auto frame = read_server_frame(conn);

  CHECK(frame.opcode == 0x1);
  CHECK(frame.payload == "worker:hello");
  CHECK(callback_on_worker.load(std::memory_order_acquire));
}

TEST_CASE("websocket worker callback failure closes connection", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession& session) {
    session.on_text([](atria::WebSocketSession&, std::string_view) {
      throw std::runtime_error{"boom"};
    });
  });

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) {
    cfg.worker_threads = 1;
    cfg.websocket_worker_callbacks = true;
  });

  auto conn = open_websocket(server.port);
  REQUIRE(atria::platform::send_all(conn, masked_client_frame(0x1, "hello")).has_value());
  auto frame = read_server_frame(conn);

  CHECK(close_code(frame) == 1011);
}

TEST_CASE("websocket sender can enqueue from another thread", "[websocket]") {
  atria::Application app;
  std::jthread producer;
  app.websocket("/ws", [&producer](atria::WebSocketSession& session) {
    atria::WebSocketSender sender = session.sender();
    producer = std::jthread{[sender] {
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
      sender.send_text("from-thread");
    }};
  });

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  auto frame = read_server_frame(conn);
  CHECK(frame.opcode == 0x1);
  CHECK(frame.payload == "from-thread");
}

TEST_CASE("websocket path params are available on the session request", "[websocket]") {
  atria::Application app;
  app.websocket("/ws/{room}", [](atria::WebSocketSession& session) {
    session.on_text([](atria::WebSocketSession& ws, std::string_view message) {
      std::string reply{ws.request().path_param("room").value_or("missing")};
      reply.push_back(':');
      reply.append(message);
      ws.send_text(std::move(reply));
    });
  });

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port, "/ws/lobby");

  REQUIRE(atria::platform::send_all(conn, masked_client_frame(0x1, "hello")).has_value());
  auto frame = read_server_frame(conn);
  CHECK(frame.opcode == 0x1);
  CHECK(frame.payload == "lobby:hello");
}

TEST_CASE("websocket responds to ping with pong", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  REQUIRE(atria::platform::send_all(conn, masked_client_frame(0x9, "beat")).has_value());
  auto frame = read_server_frame(conn);
  CHECK(frame.opcode == 0xA);
  CHECK(frame.payload == "beat");
}

TEST_CASE("websocket echoes fragmented text messages", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession& session) {
    session.on_text([](atria::WebSocketSession& ws, std::string_view message) {
      std::string reply = "joined:";
      reply.append(message);
      ws.send_text(std::move(reply));
    });
  });

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  std::string frames;
  frames.append(client_frame(0x1, "hel", true, false));
  frames.append(client_frame(0x0, "lo", true, true));
  REQUIRE(atria::platform::send_all(conn, frames).has_value());

  auto frame = read_server_frame(conn);
  CHECK(frame.opcode == 0x1);
  CHECK(frame.payload == "joined:hello");
}

TEST_CASE("websocket close handshake returns a close frame", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  REQUIRE(atria::platform::send_all(conn, client_frame(0x8, close_payload(1000))).has_value());
  auto frame = read_server_frame(conn);
  CHECK(close_code(frame) == 1000);
}

TEST_CASE("websocket rejects unmasked client frames", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  REQUIRE(atria::platform::send_all(conn, client_frame(0x1, "oops", false)).has_value());
  auto frame = read_server_frame(conn);
  CHECK(close_code(frame) == 1002);
}

TEST_CASE("websocket rejects invalid opcodes", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  REQUIRE(atria::platform::send_all(conn, client_frame(0xB, "", true)).has_value());
  auto frame = read_server_frame(conn);
  CHECK(close_code(frame) == 1002);
}

TEST_CASE("websocket rejects oversized frames", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) { cfg.max_websocket_frame_bytes = 4; });
  auto conn = open_websocket(server.port);

  REQUIRE(atria::platform::send_all(conn, masked_client_frame(0x1, "hello")).has_value());
  auto frame = read_server_frame(conn);
  CHECK(close_code(frame) == 1009);
}

TEST_CASE("websocket rejects oversized reassembled messages", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& cfg) { cfg.max_websocket_message_bytes = 4; });
  auto conn = open_websocket(server.port);

  std::string frames;
  frames.append(client_frame(0x1, "he", true, false));
  frames.append(client_frame(0x0, "llo", true, true));
  REQUIRE(atria::platform::send_all(conn, frames).has_value());

  auto frame = read_server_frame(conn);
  CHECK(close_code(frame) == 1009);
}

TEST_CASE("websocket rejects invalid handshake key", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());

  std::string request = websocket_handshake();
  auto key_pos = request.find("Sec-WebSocket-Key:");
  REQUIRE(key_pos != std::string::npos);
  auto line_end = request.find("\r\n", key_pos);
  REQUIRE(line_end != std::string::npos);
  request.replace(key_pos, line_end - key_pos, "Sec-WebSocket-Key: invalid");
  REQUIRE(atria::platform::send_all(*conn, request).has_value());

  std::string response = read_all(*conn);
  CHECK(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
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
  request.insert(request.find("\r\n\r\n"), "\r\nSec-WebSocket-Extensions: permessage-deflate");
  REQUIRE(atria::platform::send_all(*conn, request).has_value());

  std::string response = read_all(*conn);
  CHECK(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
  CHECK(response.find("websocket extensions are disabled") != std::string::npos);
}

TEST_CASE("websocket rejects malformed extension offers", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());

  std::string request = websocket_handshake();
  request.insert(
      request.find("\r\n\r\n"),
      "\r\nSec-WebSocket-Extensions: permessage-deflate; =bad"
  );
  REQUIRE(atria::platform::send_all(*conn, request).has_value());

  std::string response = read_all(*conn);
  CHECK(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
  CHECK(response.find("invalid websocket extension offer") != std::string::npos);
}

TEST_CASE("websocket rejects extension offers beyond configured limit", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& config) {
    config.max_websocket_extension_count = 1;
  });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());

  std::string request = websocket_handshake();
  request.insert(
      request.find("\r\n\r\n"),
      "\r\nSec-WebSocket-Extensions: permessage-deflate, x-test-extension"
  );
  REQUIRE(atria::platform::send_all(*conn, request).has_value());

  std::string response = read_all(*conn);
  CHECK(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
  CHECK(response.find("too many websocket extension offers") != std::string::npos);
}

TEST_CASE("websocket can ignore extension offers by policy", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& config) {
    config.websocket_extension_policy = atria::WebSocketExtensionPolicy::Ignore;
  });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());

  std::string request = websocket_handshake();
  request.insert(
      request.find("\r\n\r\n"),
      "\r\nSec-WebSocket-Extensions: permessage-deflate; client_max_window_bits"
  );
  REQUIRE(atria::platform::send_all(*conn, request).has_value());

  std::string response = read_until(*conn, "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
  CHECK(!response.contains("Sec-WebSocket-Extensions:"));
}

TEST_CASE("websocket rejects reserved bits after ignored extension offers", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app, [](atria::ServerConfig& config) {
    config.websocket_extension_policy = atria::WebSocketExtensionPolicy::Ignore;
  });

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());

  std::string request = websocket_handshake();
  request.insert(request.find("\r\n\r\n"), "\r\nSec-WebSocket-Extensions: permessage-deflate");
  REQUIRE(atria::platform::send_all(*conn, request).has_value());
  std::string response = read_until(*conn, "\r\n\r\n");
  REQUIRE(response.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);

  std::string frame = masked_client_frame(0x1, "compressed?");
  frame.front() = static_cast<char>(static_cast<unsigned char>(frame.front()) | 0x40U);
  REQUIRE(atria::platform::send_all(*conn, frame).has_value());

  auto close = read_server_frame(*conn);
  CHECK(close_code(close) == 1002);
}

TEST_CASE("websocket rejects invalid utf-8 in text frames", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  // 0xFF is an invalid UTF-8 start byte
  std::string invalid_utf8 = "\xff\xff";
  REQUIRE(atria::platform::send_all(conn, masked_client_frame(0x1, invalid_utf8)).has_value());
  auto frame = read_server_frame(conn);
  CHECK(close_code(frame) == 1007);
}

TEST_CASE("websocket rejects invalid utf-8 in fragmented text frames", "[websocket]") {
  atria::Application app;
  app.websocket("/ws", [](atria::WebSocketSession&) {});

  RunningServer server;
  start_server(server, app);
  auto conn = open_websocket(server.port);

  std::string frames;
  frames.append(client_frame(0x1, "\xff", true, false));
  frames.append(client_frame(0x0, "\xff", true, true));
  REQUIRE(atria::platform::send_all(conn, frames).has_value());

  auto frame = read_server_frame(conn);
  CHECK(close_code(frame) == 1007);
}
