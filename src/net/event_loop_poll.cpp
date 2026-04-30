// Portable poll()/WSAPoll() event loop. Used on Windows and as fallback where epoll/kqueue
// are not available. Single-threaded, level-triggered.

#include "net/event_loop.hpp"

#include "platform/socket.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#define ATRIA_POLL ::WSAPoll
using PollFd = WSAPOLLFD;
#else
#include <poll.h>
#include <unistd.h>
#define ATRIA_POLL ::poll
using PollFd = pollfd;
#endif

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atria::net {

namespace {

class PollEventLoop final : public EventLoop {
 public:
  void watch(platform::NativeSocket fd, IoEvent events, IoCallback cb) override {
    callbacks_[fd] = std::move(cb);
    interest_[fd] = events;
    rebuild_ = true;
  }

  void modify(platform::NativeSocket fd, IoEvent events) override {
    auto it = interest_.find(fd);
    if (it != interest_.end()) {
      it->second = events;
      rebuild_ = true;
    }
  }

  void unwatch(platform::NativeSocket fd) override {
    callbacks_.erase(fd);
    interest_.erase(fd);
    rebuild_ = true;
  }

  int run() override {
    stop_.store(false);
    while (!stop_.load() && !interest_.empty()) {
      run_once(200);
    }
    return 0;
  }

  void run_once(int timeout_ms) override {
    if (interest_.empty()) {
      return;
    }
    if (rebuild_) {
      fds_.clear();
      fds_.reserve(interest_.size());
      for (const auto& [fd, ev] : interest_) {
        PollFd pfd{};
        pfd.fd = fd;
        short flags = 0;
        if (any(ev & IoEvent::Read)) {
          flags |= POLLIN;
        }
        if (any(ev & IoEvent::Write)) {
          flags |= POLLOUT;
        }
        pfd.events = flags;
        fds_.push_back(pfd);
      }
      rebuild_ = false;
    }
#if defined(_WIN32)
    int rc = ATRIA_POLL(fds_.data(), static_cast<ULONG>(fds_.size()), timeout_ms);
#else
    int rc = ATRIA_POLL(fds_.data(), static_cast<nfds_t>(fds_.size()), timeout_ms);
#endif
    if (rc <= 0) {
      return;
    }
    std::vector<std::pair<platform::NativeSocket, IoEvent>> ready;
    ready.reserve(static_cast<std::size_t>(rc));
    for (const auto& pfd : fds_) {
      IoEvent fired = IoEvent::None;
      if ((pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        fired = fired | IoEvent::Read;
      }
      if ((pfd.revents & POLLOUT) != 0) {
        fired = fired | IoEvent::Write;
      }
      if (any(fired)) {
        ready.emplace_back(pfd.fd, fired);
      }
    }
    for (const auto& [fd, ev] : ready) {
      auto it = callbacks_.find(fd);
      if (it != callbacks_.end()) {
        IoCallback cb = it->second;
        cb(ev);
      }
    }
  }

  void stop() override { stop_.store(true); }

  [[nodiscard]] bool has_watches() const noexcept override { return !interest_.empty(); }

 private:
  std::unordered_map<platform::NativeSocket, IoCallback> callbacks_;
  std::unordered_map<platform::NativeSocket, IoEvent> interest_;
  std::vector<PollFd> fds_;
  bool rebuild_{true};
  std::atomic<bool> stop_{false};
};

}  // namespace

#if !defined(__linux__) && !(defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
                              defined(__OpenBSD__) || defined(__DragonFly__))
std::unique_ptr<EventLoop> make_event_loop() {
  return std::make_unique<PollEventLoop>();
}
#endif

// Always-available factory for tests / callers that want to force the portable backend.
[[nodiscard]] std::unique_ptr<EventLoop> make_poll_event_loop() {
  return std::make_unique<PollEventLoop>();
}

}  // namespace atria::net
