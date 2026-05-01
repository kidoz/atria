// Streaming Response tests:
//   * unit: Response::stream() constructs a streaming response; serialize_headers() emits
//     status line + headers + terminator without the body
//   * integration: chunked Transfer-Encoding output across the wire; Content-Length raw
//     output; many small chunks; empty stream

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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using atria::Response;
using atria::Status;

namespace {

constexpr std::string_view kHost = "127.0.0.1";

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

}  // namespace

TEST_CASE("Response::stream produces a streaming response", "[response][streaming]") {
  int call_count = 0;
  auto provider = [&call_count]() -> std::optional<std::string> {
    if (call_count == 0) {
      ++call_count;
      return std::string{"hello"};
    }
    return std::nullopt;
  };
  Response r = Response::stream(provider);
  CHECK(r.is_streaming());
  CHECK_FALSE(r.content_length().has_value());

  std::string headers = r.serialize_headers();
  CHECK(headers.find("HTTP/1.1 200 OK\r\n") == 0);
  // Note: Content-Type defaults to application/octet-stream for streams.
  CHECK(headers.find("Content-Type: application/octet-stream") != std::string::npos);
  CHECK(headers.find("\r\n\r\n") != std::string::npos);
  // The provider hasn't been called yet — streaming defers to the runtime.
  CHECK(call_count == 0);
}

TEST_CASE("Response::stream with content_length records the size", "[response][streaming]") {
  auto provider = []() -> std::optional<std::string> { return std::nullopt; };
  Response r = Response::stream(provider, /*content_length=*/std::size_t{12});
  REQUIRE(r.content_length().has_value());
  CHECK(*r.content_length() == 12);
}

TEST_CASE("server streams a chunked response over the wire", "[streaming][integration]") {
  atria::Application app;
  app.get("/big", [](atria::Request&) {
    auto state = std::make_shared<int>(0);
    auto provider = [state]() -> std::optional<std::string> {
      if (*state >= 4) {
        return std::nullopt;
      }
      ++(*state);
      return std::string{"chunk"} + std::to_string(*state);
    };
    return Response::stream(provider);
  });

  RunningServer server;
  server.port = pick_port();
  REQUIRE(server.port != 0);
  server.thread = std::thread{[&] {
    atria::ServerConfig cfg;
    cfg.host = std::string{kHost};
    cfg.port = server.port;
    cfg.worker_threads = 0;  // synchronous for deterministic test
    app.listen(cfg);
  }};
  server.app = &app;

  // Wait for ready.
  for (int i = 0; i < 100; ++i) {
    auto c = atria::platform::connect_tcp(kHost, server.port);
    if (c.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(
      atria::platform::send_all(
          *conn,
          std::string_view{"GET /big HTTP/1.1\r\n"
                           "Host: 127.0.0.1\r\n"
                           "Connection: close\r\n"
                           "\r\n"}
      )
          .has_value()
  );
  std::string raw = drain(*conn);

  CHECK(raw.find("HTTP/1.1 200 OK\r\n") == 0);
  CHECK(raw.find("Transfer-Encoding: chunked") != std::string::npos);
  CHECK(raw.find("Content-Length:") == std::string::npos);

  // Body after \r\n\r\n: chunk-size hex, CRLF, data, CRLF, ..., 0\r\n\r\n
  auto body_pos = raw.find("\r\n\r\n");
  REQUIRE(body_pos != std::string::npos);
  std::string body = raw.substr(body_pos + 4);

  // Decode: each chunk line is hex, expected payloads are chunk1..chunk4.
  CHECK(body.find("6\r\nchunk1\r\n") != std::string::npos);
  CHECK(body.find("6\r\nchunk2\r\n") != std::string::npos);
  CHECK(body.find("6\r\nchunk3\r\n") != std::string::npos);
  CHECK(body.find("6\r\nchunk4\r\n") != std::string::npos);
  // Terminator
  CHECK(body.rfind("0\r\n\r\n") != std::string::npos);
}

TEST_CASE("server streams with Content-Length when known", "[streaming][integration]") {
  atria::Application app;
  app.get("/sized", [](atria::Request&) {
    auto state = std::make_shared<int>(0);
    auto provider = [state]() -> std::optional<std::string> {
      if (*state >= 3) {
        return std::nullopt;
      }
      ++(*state);
      return std::string{"abcd"};  // 3 chunks * 4 bytes = 12 bytes total
    };
    return Response::stream(provider, /*content_length=*/std::size_t{12});
  });

  RunningServer server;
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

  for (int i = 0; i < 100; ++i) {
    auto c = atria::platform::connect_tcp(kHost, server.port);
    if (c.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(
      atria::platform::send_all(
          *conn,
          std::string_view{"GET /sized HTTP/1.1\r\n"
                           "Host: 127.0.0.1\r\n"
                           "Connection: close\r\n"
                           "\r\n"}
      )
          .has_value()
  );
  std::string raw = drain(*conn);

  CHECK(raw.find("Content-Length: 12") != std::string::npos);
  CHECK(raw.find("Transfer-Encoding") == std::string::npos);
  // Body is exactly the 12 bytes, no chunk framing.
  auto body_pos = raw.find("\r\n\r\n");
  REQUIRE(body_pos != std::string::npos);
  std::string body = raw.substr(body_pos + 4);
  CHECK(body == "abcdabcdabcd");
}

TEST_CASE("server streams empty body cleanly", "[streaming][integration]") {
  atria::Application app;
  app.get("/empty", [](atria::Request&) {
    auto provider = []() -> std::optional<std::string> { return std::nullopt; };
    return Response::stream(provider);
  });

  RunningServer server;
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

  for (int i = 0; i < 100; ++i) {
    auto c = atria::platform::connect_tcp(kHost, server.port);
    if (c.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(
      atria::platform::send_all(
          *conn,
          std::string_view{"GET /empty HTTP/1.1\r\n"
                           "Host: 127.0.0.1\r\n"
                           "Connection: close\r\n"
                           "\r\n"}
      )
          .has_value()
  );
  std::string raw = drain(*conn);
  CHECK(raw.find("HTTP/1.1 200 OK\r\n") == 0);
  CHECK(raw.find("Transfer-Encoding: chunked") != std::string::npos);
  // Just the terminator.
  auto body_pos = raw.find("\r\n\r\n");
  REQUIRE(body_pos != std::string::npos);
  std::string body = raw.substr(body_pos + 4);
  CHECK(body == "0\r\n\r\n");
}

TEST_CASE("wakeable stream resumes when producer wakes the loop", "[streaming][integration]") {
  struct State {
    std::mutex mu;
    std::vector<std::string> chunks;
    std::optional<atria::StreamWaker> waker;
    bool done{false};
  };

  auto state = std::make_shared<State>();
  atria::Application app;
  app.get("/events", [state](atria::Request&) {
    auto provider = [state](atria::StreamWaker& waker) -> std::optional<std::string> {
      std::lock_guard<std::mutex> lock(state->mu);
      state->waker = waker;
      if (!state->chunks.empty()) {
        std::string next = std::move(state->chunks.front());
        state->chunks.erase(state->chunks.begin());
        return next;
      }
      if (state->done) {
        return std::string{};
      }
      return std::nullopt;
    };
    auto response = Response::stream_wakeable(provider);
    response.set_header("Content-Type", "text/event-stream");
    return response;
  });

  RunningServer server;
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

  for (int i = 0; i < 100; ++i) {
    auto c = atria::platform::connect_tcp(kHost, server.port);
    if (c.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  auto conn = atria::platform::connect_tcp(kHost, server.port);
  REQUIRE(conn.has_value());
  REQUIRE(atria::platform::set_recv_timeout(*conn, 2000).has_value());
  REQUIRE(
      atria::platform::send_all(
          *conn,
          std::string_view{"GET /events HTTP/1.1\r\n"
                           "Host: 127.0.0.1\r\n"
                           "Connection: close\r\n"
                           "\r\n"}
      )
          .has_value()
  );

  std::string raw = read_until(*conn, "\r\n\r\n");
  CHECK(raw.find("HTTP/1.1 200 OK\r\n") == 0);
  CHECK(raw.find("Transfer-Encoding: chunked") != std::string::npos);
  CHECK(raw.find("Content-Type: text/event-stream") != std::string::npos);

  for (int i = 0; i < 100; ++i) {
    {
      std::lock_guard<std::mutex> lock(state->mu);
      if (state->waker.has_value()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  atria::StreamWaker waker;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    REQUIRE(state->waker.has_value());
    state->chunks.push_back("data: one\n\n");
    waker = *state->waker;
  }
  waker.wake();

  raw.append(read_until(*conn, "data: one\n\n"));
  CHECK(raw.find("b\r\ndata: one\n\n\r\n") != std::string::npos);

  {
    std::lock_guard<std::mutex> lock(state->mu);
    state->done = true;
    waker = *state->waker;
  }
  waker.wake();

  raw.append(read_until(*conn, "0\r\n\r\n"));
  CHECK(raw.find("0\r\n\r\n") != std::string::npos);
}
