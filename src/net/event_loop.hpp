// Cross-platform event loop interface.
//
// Single-threaded readiness-based loop. Callers register interest in read/write readiness
// for a socket; the loop dispatches a callback when the OS reports the socket is ready.
//
// Three backends are shipped:
//   * event_loop_epoll.cpp   on Linux  (epoll, level-triggered)
//   * event_loop_kqueue.cpp  on macOS / BSD (kqueue)
//   * event_loop_poll.cpp    everywhere else, including Windows via WSAPoll
//
// All three are *readiness*-based. Windows IOCP is completion-based and would require a
// different interface; it is intentionally out of scope here. WSAPoll is a sufficient
// stopgap for Windows in this phase. Promotion to native IOCP is tracked in the roadmap.

#pragma once

#include "platform/socket.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace atria::net {

enum class IoEvent : std::uint8_t {
  None = 0,
  Read = 1 << 0,
  Write = 1 << 1,
};

[[nodiscard]] constexpr IoEvent operator|(IoEvent a, IoEvent b) noexcept {
  return static_cast<IoEvent>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr IoEvent operator&(IoEvent a, IoEvent b) noexcept {
  return static_cast<IoEvent>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr bool any(IoEvent a) noexcept { return static_cast<std::uint8_t>(a) != 0; }

// Callback invoked with the events that fired (Read, Write, or both).
using IoCallback = std::function<void(IoEvent)>;

class EventLoop {
 public:
  EventLoop() = default;
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;
  EventLoop(EventLoop&&) = delete;
  EventLoop& operator=(EventLoop&&) = delete;
  virtual ~EventLoop() = default;

  // Register a socket for the given event mask. The callback is owned by the loop.
  // Caller must call unwatch() before destroying the socket.
  virtual void watch(platform::NativeSocket fd, IoEvent events, IoCallback cb) = 0;

  // Change the event mask for an already-watched socket.
  virtual void modify(platform::NativeSocket fd, IoEvent events) = 0;

  // Stop watching a socket.
  virtual void unwatch(platform::NativeSocket fd) = 0;

  // Run the loop until stop() is called or every watched fd is unwatched.
  virtual int run() = 0;

  // Single iteration: block up to `timeout_ms` waiting for readiness, dispatch any ready
  // callbacks, then return. Useful when the caller wants to interleave its own periodic
  // work (e.g. idle-connection sweep) between event polls.
  virtual void run_once(int timeout_ms) = 0;

  // Request the loop to stop. Safe to call from any thread.
  virtual void stop() = 0;

  // Returns true if at least one fd is currently watched.
  [[nodiscard]] virtual bool has_watches() const noexcept = 0;
};

// Factory: returns the best available backend for the current platform.
[[nodiscard]] std::unique_ptr<EventLoop> make_event_loop();

}  // namespace atria::net
