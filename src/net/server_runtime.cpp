// Event-driven HTTP runtime with optional worker-pool offload for slow handlers.
//
// Threading model:
//   * One EventLoop on the calling thread (epoll on Linux, kqueue on macOS/BSD,
//     poll/WSAPoll elsewhere).
//   * Optional WorkerPool of `ServerConfig::worker_threads` threads. When non-zero, parsed
//     requests are submitted to the pool; the worker thread runs `Application::dispatch`
//     and pushes the resulting Response onto a completion queue.
//   * A Notifier (pipe on POSIX / TCP loopback pair on Windows) wakes the loop when a
//     completion, external WebSocket send, or wakeable stream signal is available. The
//     loop drains queued work and resumes each connection's state machine.
//
// Handlers therefore run on the worker pool when one is configured; otherwise they run
// synchronously on the loop thread (current behavior). Either way, all I/O — accept,
// read, parse, write — happens on the loop thread.

#include "net/server_runtime.hpp"

#include "atria/application.hpp"
#include "atria/server_config.hpp"
#include "net/connection.hpp"
#include "net/event_loop.hpp"
#include "net/notifier.hpp"
#include "net/tcp_listener.hpp"
#include "net/worker_pool.hpp"
#include "platform/socket.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atria::net {

ServerRuntime::ServerRuntime(Application& app, const ServerConfig& config)
    : app_(app), config_(config) {}

std::string ServerRuntime::read_request_bytes(platform::SocketHandle& /*conn*/) {
  return {};
}

void ServerRuntime::handle_connection(platform::SocketHandle /*conn*/) {}

namespace {

struct Completion {
  std::shared_ptr<Connection> conn;
  Response response;
};

struct LoopTask {
  std::weak_ptr<Connection> conn;
  ConnectionTask task;
};

struct LoopSignal {
  std::unique_ptr<Notifier> notifier;
  std::mutex tasks_mu;
  std::queue<LoopTask> tasks;
  std::atomic<bool> accepting{true};

  void post(std::weak_ptr<Connection> conn, ConnectionTask task) {
    if (!accepting.load()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(tasks_mu);
      tasks.push(LoopTask{std::move(conn), std::move(task)});
    }
    notifier->notify();
  }
};

}  // namespace

int ServerRuntime::run(std::atomic<bool>& running) {
  platform::global_init();

  auto listener =
      TcpListener::bind(config_.host, config_.port, static_cast<int>(config_.accept_backlog));
  if (!listener.has_value()) {
    std::fprintf(stderr, "[atria] listen failed: %s\n", listener.error().message.c_str());
    return 1;
  }
  if (auto nb = platform::set_nonblocking(listener->handle(), true); !nb.has_value()) {
    std::fprintf(
        stderr,
        "[atria] set_nonblocking(listener) failed: %s\n",
        nb.error().message.c_str()
    );
    return 1;
  }
  std::fprintf(
      stderr,
      "[atria] listening on %s:%u\n",
      config_.host.c_str(),
      static_cast<unsigned>(config_.port)
  );

  auto loop = make_event_loop();
  if (!loop) {
    std::fprintf(stderr, "[atria] failed to create event loop\n");
    return 1;
  }

  auto loop_signal = std::make_shared<LoopSignal>();
  loop_signal->notifier = Notifier::create();
  if (!loop_signal->notifier) {
    std::fprintf(stderr, "[atria] failed to create notifier\n");
    return 1;
  }

  // Optional worker pool. Zero = synchronous dispatch on the loop thread (current behavior).
  std::unique_ptr<WorkerPool> pool;
  std::mutex completions_mu;
  std::queue<Completion> completions;

  if (config_.worker_threads > 0) {
    pool = std::make_unique<WorkerPool>(config_.worker_threads);
  }

  std::unordered_map<platform::NativeSocket, std::shared_ptr<Connection>> conns;
  // Per-fd peer IP, kept until the connection is removed so we can decrement the right
  // bucket. Map separately because Connection itself doesn't carry the peer.
  std::unordered_map<platform::NativeSocket, std::string> peer_of;
  std::unordered_map<std::string, std::size_t> conns_per_ip;

  auto remove_connection = [&](platform::NativeSocket fd) {
    loop->unwatch(fd);
    if (auto peer_entry = peer_of.find(fd); peer_entry != peer_of.end()) {
      auto count_entry = conns_per_ip.find(peer_entry->second);
      if (count_entry != conns_per_ip.end()) {
        if (--count_entry->second == 0) {
          conns_per_ip.erase(count_entry);
        }
      }
      peer_of.erase(peer_entry);
    }
    conns.erase(fd);
  };

  auto connection_io_mask = [](Connection& connection) -> IoEvent {
    switch (connection.state()) {
    case ConnectionState::Reading:
      return IoEvent::Read;
    case ConnectionState::Writing:
      return connection.wants_write() ? IoEvent::Write : IoEvent::None;
    case ConnectionState::WebSocket:
      return connection.wants_write() ? (IoEvent::Read | IoEvent::Write) : IoEvent::Read;
    case ConnectionState::WebSocketDispatching:
      return connection.wants_write() ? IoEvent::Write : IoEvent::None;
    case ConnectionState::Dispatching:
    case ConnectionState::Closing:
      return IoEvent::None;
    }
    return IoEvent::None;
  };

  // Build the dispatch hook that the Connection uses when offloading to the worker pool.
  // The closure captures by raw pointer-ish state but uses shared_ptr<Connection> for
  // lifetime. The completion is posted to the completion queue; the loop wakes via the
  // notifier and drains.
  DispatchHook dispatch_hook;
  if (pool) {
    dispatch_hook = [&app = app_, &pool, loop_signal, &completions_mu, &completions](
                        std::shared_ptr<Connection> conn,
                        Request request
                    ) {
      pool->submit([&app,
                    loop_signal,
                    &completions_mu,
                    &completions,
                    conn = std::move(conn),
                    request = std::move(request)]() mutable {
        Response response = app.dispatch(request);
        {
          std::lock_guard<std::mutex> lock(completions_mu);
          completions.push(Completion{std::move(conn), std::move(response)});
        }
        loop_signal->notifier->notify();
      });
    };
  }

  WebSocketCallbackHook websocket_callback_hook;
  if (pool && config_.websocket_worker_callbacks) {
    websocket_callback_hook = [&pool](std::function<void()> callback) {
      pool->submit(std::move(callback));
    };
  }

  LoopTaskHook loop_task_hook = [loop_signal](std::weak_ptr<Connection> conn, ConnectionTask task) {
    loop_signal->post(std::move(conn), std::move(task));
  };

  auto on_listener_readable = [&](IoEvent /*ev*/) {
    while (true) {
      std::string peer_ip;
      auto accepted = platform::accept_connection_with_peer(listener->handle(), peer_ip);
      if (!accepted.has_value()) {
        return;
      }

      // Enforce connection budgets. Limits of 0 mean "unlimited."
      if (config_.max_connections != 0 && conns.size() >= config_.max_connections) {
        // Drop silently: SocketHandle dtor closes the socket.
        continue;
      }
      if (config_.max_connections_per_ip != 0) {
        auto it = conns_per_ip.find(peer_ip);
        if (it != conns_per_ip.end() && it->second >= config_.max_connections_per_ip) {
          continue;
        }
      }

      if (auto nb = platform::set_nonblocking(*accepted, true); !nb.has_value()) {
        continue;
      }
      auto fd = accepted->native();
      auto conn = std::make_shared<Connection>(
          std::move(*accepted),
          app_,
          config_,
          dispatch_hook,
          websocket_callback_hook,
          loop_task_hook
      );
      Connection* raw = conn.get();
      conns.emplace(fd, std::move(conn));
      peer_of.emplace(fd, peer_ip);
      ++conns_per_ip[peer_ip];
      auto cb = [raw, &remove_connection, &loop, &connection_io_mask](IoEvent ev) {
        if (any(ev & IoEvent::Read)) {
          raw->on_readable();
        }
        if (any(ev & IoEvent::Write)) {
          raw->on_writable();
        }
        if (raw->is_closing()) {
          remove_connection(raw->fd());
          return;
        }
        loop->modify(raw->fd(), connection_io_mask(*raw));
      };
      loop->watch(fd, IoEvent::Read, std::move(cb));
    }
  };

  loop->watch(listener->handle().native(), IoEvent::Read, std::move(on_listener_readable));

  // Notifier read end: drain bytes, then deliver pending completions and connection tasks
  // on the loop thread.
  {
    auto drain_wakeups = [&, loop_signal](IoEvent /*ev*/) {
      loop_signal->notifier->drain();

      std::queue<Completion> pending_completions;
      {
        std::lock_guard<std::mutex> lock(completions_mu);
        std::swap(pending_completions, completions);
      }
      while (!pending_completions.empty()) {
        Completion c = std::move(pending_completions.front());
        pending_completions.pop();
        auto fd = c.conn->fd();
        auto it = conns.find(fd);
        if (it == conns.end() || it->second.get() != c.conn.get()) {
          // Connection was closed (timeout / peer reset) while the worker ran.
          continue;
        }
        c.conn->on_dispatch_complete(std::move(c.response));
        if (c.conn->is_closing()) {
          remove_connection(fd);
          continue;
        }
        loop->modify(fd, connection_io_mask(*c.conn));
      }

      std::queue<LoopTask> pending_tasks;
      {
        std::lock_guard<std::mutex> lock(loop_signal->tasks_mu);
        std::swap(pending_tasks, loop_signal->tasks);
      }
      while (!pending_tasks.empty()) {
        LoopTask queued = std::move(pending_tasks.front());
        pending_tasks.pop();
        auto conn = queued.conn.lock();
        if (!conn) {
          continue;
        }
        auto fd = conn->fd();
        auto it = conns.find(fd);
        if (it == conns.end() || it->second.get() != conn.get()) {
          continue;
        }
        queued.task(*conn);
        if (conn->is_closing()) {
          remove_connection(fd);
          continue;
        }
        loop->modify(fd, connection_io_mask(*conn));
      }
    };
    loop->watch(loop_signal->notifier->read_fd(), IoEvent::Read, std::move(drain_wakeups));
  }

  running.store(true);

  using Clock = Connection::Clock;
  Clock::time_point last_sweep = Clock::now();

  while (running.load()) {
    loop->run_once(200);

    auto now = Clock::now();
    if (now - last_sweep >= std::chrono::milliseconds{200}) {
      last_sweep = now;
      std::vector<platform::NativeSocket> overdue;
      for (const auto& [fd, c] : conns) {
        if (c->is_overdue(now)) {
          overdue.push_back(fd);
        }
      }
      for (auto fd : overdue) {
        remove_connection(fd);
      }
    }
  }

  // Graceful drain: stop accepting; give in-flight connections a brief window to finish.
  loop->unwatch(listener->handle().native());
  auto deadline = Clock::now() + std::chrono::milliseconds{500};
  while (!conns.empty() && Clock::now() < deadline) {
    loop->run_once(50);
  }
  loop_signal->accepting.store(false);
  loop->unwatch(loop_signal->notifier->read_fd());
  conns.clear();
  // Join workers while completion queues and the notifier state are still alive.
  pool.reset();
  // Any late completions now only release Connection refs when the queue dies.
  return 0;
}

}  // namespace atria::net
