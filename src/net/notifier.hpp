// Cross-platform event-loop wakeup.
//
// A Notifier owns one or two OS-level handles whose read end can be watched by the
// EventLoop. notify() makes the read end readable; the loop's callback for that read fd
// drains the bytes and runs whatever action the runtime wants (typically: drain the
// completion queue posted by worker threads).
//
// POSIX backend uses pipe(); Windows backend uses a TCP loopback pair (because Winsock
// has no socketpair() and select/WSAPoll only handles socket handles, not pipe handles).

#pragma once

#include "platform/socket.hpp"

#include <memory>

namespace atria::net {

class Notifier {
 public:
  Notifier(const Notifier&) = delete;
  Notifier& operator=(const Notifier&) = delete;
  Notifier(Notifier&&) = delete;
  Notifier& operator=(Notifier&&) = delete;

  ~Notifier();

  // Returns a usable Notifier or nullptr on platform-level allocation failure.
  [[nodiscard]] static std::unique_ptr<Notifier> create();

  // The fd to watch for read readiness on the EventLoop.
  [[nodiscard]] platform::NativeSocket read_fd() const noexcept;

  // Make read_fd() readable. Safe to call from any thread.
  void notify();

  // Drain all pending bytes from read_fd(). Call after the EventLoop reports readability.
  void drain();

 private:
  Notifier();

#if defined(_WIN32)
  platform::SocketHandle read_;
  platform::SocketHandle write_;
#else
  int read_fd_{-1};
  int write_fd_{-1};
#endif
};

}  // namespace atria::net
