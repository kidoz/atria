// Unit tests for net::WorkerPool and parallel-handler integration test.

#include "atria/application.hpp"
#include "atria/json.hpp"
#include "atria/middleware.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"
#include "net/worker_pool.hpp"
#include "platform/socket.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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

[[nodiscard]] std::uint16_t pick_port() {
  auto probe = atria::platform::listen_tcp(kHost, 0, 4);
  if (!probe.has_value()) {
    return 0;
  }
  auto port = atria::platform::local_port(*probe);
  return port.value_or(0);
}

}  // namespace

TEST_CASE("WorkerPool runs jobs concurrently", "[worker_pool]") {
  atria::net::WorkerPool pool{4};
  REQUIRE(pool.size() == 4);

  std::atomic<int> running{0};
  std::atomic<int> max_running{0};
  std::atomic<int> done{0};

  for (int i = 0; i < 8; ++i) {
    pool.submit([&] {
      int now = ++running;
      int prev_max = max_running.load();
      while (prev_max < now && !max_running.compare_exchange_weak(prev_max, now)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
      --running;
      ++done;
    });
  }

  // Wait for all jobs to finish.
  while (done.load() < 8) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  CHECK(max_running.load() >= 2);  // at least some parallelism observed
  CHECK(done.load() == 8);
}

TEST_CASE("server dispatches handlers in parallel via worker pool", "[worker_pool][integration]") {
  atria::Application app;
  std::atomic<int> concurrent{0};
  std::atomic<int> peak{0};

  app.get("/slow", [&](atria::Request&) {
    int now = ++concurrent;
    int prev = peak.load();
    while (prev < now && !peak.compare_exchange_weak(prev, now)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    --concurrent;
    return atria::Response::json(atria::Json::object({{"ok", true}}));
  });

  // Find a free port and start the server in a worker thread, with worker_threads = 4.
  std::uint16_t port = pick_port();
  REQUIRE(port != 0);

  std::thread server_thread{[&app, port] {
    atria::ServerConfig cfg;
    cfg.host = std::string{kHost};
    cfg.port = port;
    cfg.worker_threads = 4;
    app.listen(cfg);
  }};

  // Wait for the server to be ready (poll-connect with retries).
  for (int i = 0; i < 100; ++i) {
    auto c = atria::platform::connect_tcp(kHost, port);
    if (c.has_value()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  // Fire 4 concurrent requests on 4 separate connections.
  auto fire = [port]() {
    auto conn = atria::platform::connect_tcp(kHost, port);
    if (!conn.has_value()) {
      return std::string{};
    }
    auto sent = atria::platform::send_all(
        *conn,
        std::string_view{"GET /slow HTTP/1.1\r\n"
                         "Host: 127.0.0.1\r\n"
                         "Connection: close\r\n"
                         "\r\n"}
    );
    if (!sent.has_value()) {
      return std::string{};
    }
    return drain(*conn);
  };

  auto t0 = std::chrono::steady_clock::now();
  std::vector<std::future<std::string>> futures;
  futures.reserve(4);
  for (int i = 0; i < 4; ++i) {
    futures.emplace_back(std::async(std::launch::async, fire));
  }
  for (auto& f : futures) {
    auto resp = f.get();
    REQUIRE(resp.find("HTTP/1.1 200 OK\r\n") == 0);
  }
  auto elapsed = std::chrono::steady_clock::now() - t0;

  // 4 sequential 80ms handlers would take ~320ms. With a worker pool of 4 they should
  // complete in ~80–150ms. Allow generous slack for CI.
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  CHECK(ms < 250);
  CHECK(peak.load() >= 2);  // at least 2 handlers ran concurrently

  // Tear down: the server is in a thread blocked on listen(). We can't ask Application
  // to stop from outside without a public shutdown — Application::shutdown() flips the
  // running atomic, so we call it.
  app.shutdown();
  // Wake the loop (in case it's idle).
  auto wake = atria::platform::connect_tcp(kHost, port);
  (void)wake;
  if (server_thread.joinable()) {
    server_thread.join();
  }
}
