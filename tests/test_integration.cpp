// Integration test: starts the Application on a real socket, exchanges HTTP bytes over loopback.
//
// Server runs in a worker thread. The test connects via the platform layer's TCP client,
// writes a raw HTTP/1.1 request, reads bytes until EOF, and asserts the response.

#include "atria/application.hpp"
#include "atria/json.hpp"
#include "atria/middleware.hpp"
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
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kHost = "127.0.0.1";

struct RunningServer {
  std::thread thread;
  atria::Application* app{nullptr};
  std::uint16_t port{0};

  ~RunningServer() {
    if (app != nullptr) {
      app->shutdown();
      // Wake the blocking accept loop with a one-shot connection.
      auto wake = atria::platform::connect_tcp(kHost, port);
      if (wake.has_value()) {
        (void)atria::platform::send_all(*wake, std::string_view{"X"});
      }
    }
    if (thread.joinable()) {
      thread.join();
    }
  }
};

[[nodiscard]] std::uint16_t
pick_free_port_and_listen(atria::Application& app, std::thread& thread) {
  std::uint16_t port = 0;
  {
    auto probe = atria::platform::listen_tcp(kHost, 0, 4);
    if (!probe.has_value()) {
      return 0;
    }
    auto assigned_port = atria::platform::local_port(*probe);
    if (!assigned_port.has_value()) {
      return 0;
    }
    port = *assigned_port;
  }
  thread = std::thread{[&app, port] { app.listen({.host = std::string{kHost}, .port = port}); }};
  // Spin until accept-loop is ready.
  for (int i = 0; i < 100; ++i) {
    auto conn = atria::platform::connect_tcp(kHost, port);
    if (conn.has_value()) {
      return port;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return port;
}

[[nodiscard]] std::string drain(atria::platform::SocketHandle& conn) {
  std::string out;
  std::array<char, 4096> buf{};
  while (true) {
    auto n = atria::platform::recv_some(conn, buf.data(), buf.size());
    if (!n.has_value() || *n == 0) {
      break;
    }
    out.append(buf.data(), *n);
  }
  return out;
}

[[nodiscard]] std::string http_exchange(std::uint16_t port, std::string_view request_bytes) {
  auto conn = atria::platform::connect_tcp(kHost, port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::send_all(*conn, request_bytes).has_value());
  return drain(*conn);
}

}  // namespace

TEST_CASE("server roundtrips a GET /health", "[integration]") {
  atria::Application app;
  app.use(atria::middleware::error_handler());
  app.get("/health", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"status", "ok"}}));
  });

  RunningServer server;
  server.port = pick_free_port_and_listen(app, server.thread);
  server.app = &app;
  REQUIRE(server.port != 0);

  std::string response = http_exchange(
      server.port,
      "GET /health HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: close\r\n"
      "\r\n"
  );

  REQUIRE(response.find("HTTP/1.1 200 OK\r\n") == 0);
  REQUIRE(response.find("Content-Type: application/json") != std::string::npos);
  REQUIRE(response.find(R"({"status":"ok"})") != std::string::npos);
}

TEST_CASE("server roundtrips a POST with body and path parameter", "[integration]") {
  atria::Application app;
  app.post("/echo/{id}", [](atria::Request& req) {
    auto id = req.path_param("id").value_or("");
    return atria::Response::json(
        atria::Json::object({
            {"id", std::string{id}},
            {"body", std::string{req.body()}},
        })
    );
  });

  RunningServer server;
  server.port = pick_free_port_and_listen(app, server.thread);
  server.app = &app;
  REQUIRE(server.port != 0);

  std::string body = R"({"k":"v"})";
  std::string request = "POST /echo/42 HTTP/1.1\r\n"
                        "Host: 127.0.0.1\r\n"
                        "Connection: close\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " +
                        std::to_string(body.size()) + "\r\n\r\n" + body;

  std::string response = http_exchange(server.port, request);

  REQUIRE(response.find("HTTP/1.1 200 OK\r\n") == 0);
  REQUIRE(response.find(R"("id":"42")") != std::string::npos);
  REQUIRE(response.find(R"("body":"{\"k\":\"v\"}")") != std::string::npos);
}

TEST_CASE("server parses and emits styled JSON keys", "[integration][json][naming]") {
  atria::Application app;
  app.post("/users", [](atria::Request& req) {
    auto parsed = req.json(atria::JsonKeyStyle::SnakeCase);
    if (!parsed.has_value()) {
      return atria::Response::empty(atria::Status::BadRequest);
    }
    const atria::Json* display_name = parsed->find("display_name");
    if (display_name == nullptr || !display_name->is_string()) {
      return atria::Response::empty(atria::Status::BadRequest);
    }
    return atria::Response::json(
        atria::Json::object({
            {"display_name", display_name->as_string()},
            {"created_at", "today"},
        }),
        atria::JsonKeyStyle::CamelCase
    );
  });

  RunningServer server;
  server.port = pick_free_port_and_listen(app, server.thread);
  server.app = &app;
  REQUIRE(server.port != 0);

  std::string body = R"({"DisplayName":"Ada"})";
  std::string request = "POST /users HTTP/1.1\r\n"
                        "Host: 127.0.0.1\r\n"
                        "Connection: close\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " +
                        std::to_string(body.size()) + "\r\n\r\n" + body;

  std::string response = http_exchange(server.port, request);

  REQUIRE(response.find("HTTP/1.1 200 OK\r\n") == 0);
  REQUIRE(response.find(R"({"displayName":"Ada","createdAt":"today"})") != std::string::npos);
}

TEST_CASE("server returns 404 for unknown route", "[integration]") {
  atria::Application app;
  app.get("/known", [](atria::Request&) { return atria::Response::empty(atria::Status::Ok); });

  RunningServer server;
  server.port = pick_free_port_and_listen(app, server.thread);
  server.app = &app;
  REQUIRE(server.port != 0);

  std::string response = http_exchange(
      server.port,
      "GET /missing HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: close\r\n"
      "\r\n"
  );

  REQUIRE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
  REQUIRE(response.find(R"("code":"not_found")") != std::string::npos);
}

TEST_CASE("server keeps connection alive for multiple requests", "[integration][keep-alive]") {
  atria::Application app;
  app.get("/n", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"hit", true}}));
  });

  RunningServer server;
  server.port = pick_free_port_and_listen(app, server.thread);
  server.app = &app;
  REQUIRE(server.port != 0);

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());

  for (int i = 0; i < 3; ++i) {
    REQUIRE(
        atria::platform::send_all(
            *conn,
            std::string_view{"GET /n HTTP/1.1\r\n"
                             "Host: 127.0.0.1\r\n"
                             "\r\n"}
        )
            .has_value()
    );

    // Read until we have one full response (Content-Length-driven).
    std::string acc;
    while (true) {
      std::array<char, 1024> buf{};
      auto n = atria::platform::recv_some(*conn, buf.data(), buf.size());
      REQUIRE(n.has_value());
      REQUIRE(*n > 0);
      acc.append(buf.data(), *n);
      if (acc.find(R"({"hit":true})") != std::string::npos) {
        break;
      }
    }
    REQUIRE(acc.find("HTTP/1.1 200 OK\r\n") == 0);
    REQUIRE(acc.find("Connection: keep-alive") != std::string::npos);
  }
}

TEST_CASE("server decodes chunked request body", "[integration][chunked]") {
  atria::Application app;
  app.post("/echo", [](atria::Request& req) {
    return atria::Response::text(std::string{req.body()});
  });

  RunningServer server;
  server.port = pick_free_port_and_listen(app, server.thread);
  server.app = &app;
  REQUIRE(server.port != 0);

  std::string raw = "POST /echo HTTP/1.1\r\n"
                    "Host: 127.0.0.1\r\n"
                    "Connection: close\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "5\r\nhello\r\n"
                    "1\r\n,\r\n"
                    "6\r\nworld!\r\n"
                    "0\r\n\r\n";
  std::string response = http_exchange(server.port, raw);
  REQUIRE(response.find("HTTP/1.1 200 OK\r\n") == 0);
  REQUIRE(response.find("\r\n\r\nhello,world!") != std::string::npos);
}

TEST_CASE("server returns 400 on malformed request", "[integration]") {
  atria::Application app;
  app.get("/anything", [](atria::Request&) { return atria::Response::empty(atria::Status::Ok); });

  RunningServer server;
  server.port = pick_free_port_and_listen(app, server.thread);
  server.app = &app;
  REQUIRE(server.port != 0);

  std::string response = http_exchange(
      server.port,
      "BREW / HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: close\r\n"
      "\r\n"
  );

  REQUIRE((response.find("HTTP/1.1 400") == 0 || response.find("HTTP/1.1 501") == 0));
}
