// epoll event loop for Linux. Level-triggered for simplicity.

#include "net/event_loop.hpp"

#if defined(__linux__)

#include "platform/socket.hpp"

#include <sys/epoll.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atria::net {

namespace {

[[nodiscard]] std::uint32_t to_epoll_mask(IoEvent ev) noexcept {
  std::uint32_t out = 0;
  if (any(ev & IoEvent::Read)) {
    out |= EPOLLIN;
  }
  if (any(ev & IoEvent::Write)) {
    out |= EPOLLOUT;
  }
  return out;
}

class EpollEventLoop final : public EventLoop {
 public:
  EpollEventLoop() : ep_(::epoll_create1(EPOLL_CLOEXEC)) {}

  ~EpollEventLoop() override {
    if (ep_ >= 0) {
      ::close(ep_);
    }
  }

  void watch(platform::NativeSocket fd, IoEvent events, IoCallback cb) override {
    callbacks_[fd] = std::move(cb);
    interest_[fd] = events;
    epoll_event ev{};
    std::memset(&ev, 0, sizeof(ev));
    ev.events = to_epoll_mask(events);
    ev.data.fd = fd;
    (void)::epoll_ctl(ep_, EPOLL_CTL_ADD, fd, &ev);
  }

  void modify(platform::NativeSocket fd, IoEvent events) override {
    interest_[fd] = events;
    epoll_event ev{};
    std::memset(&ev, 0, sizeof(ev));
    ev.events = to_epoll_mask(events);
    ev.data.fd = fd;
    (void)::epoll_ctl(ep_, EPOLL_CTL_MOD, fd, &ev);
  }

  void unwatch(platform::NativeSocket fd) override {
    callbacks_.erase(fd);
    interest_.erase(fd);
    (void)::epoll_ctl(ep_, EPOLL_CTL_DEL, fd, nullptr);
  }

  int run() override {
    if (ep_ < 0) {
      return 1;
    }
    stop_.store(false);
    while (!stop_.load() && !interest_.empty()) {
      run_once(200);
    }
    return 0;
  }

  void run_once(int timeout_ms) override {
    if (ep_ < 0 || interest_.empty()) {
      return;
    }
    constexpr int kBatch = 64;
    std::vector<epoll_event> events(kBatch);
    int n = ::epoll_wait(ep_, events.data(), kBatch, timeout_ms);
    if (n <= 0) {
      return;
    }
    std::vector<std::pair<platform::NativeSocket, IoEvent>> ready;
    auto count = static_cast<std::size_t>(n);
    ready.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      auto fd = static_cast<platform::NativeSocket>(events[i].data.fd);
      IoEvent ev = IoEvent::None;
      if ((events[i].events & (EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLERR)) != 0) {
        ev = ev | IoEvent::Read;
      }
      if ((events[i].events & EPOLLOUT) != 0) {
        ev = ev | IoEvent::Write;
      }
      if (any(ev)) {
        ready.emplace_back(fd, ev);
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
  int ep_;
  std::unordered_map<platform::NativeSocket, IoCallback> callbacks_;
  std::unordered_map<platform::NativeSocket, IoEvent> interest_;
  std::atomic<bool> stop_{false};
};

}  // namespace

std::unique_ptr<EventLoop> make_event_loop() {
  return std::make_unique<EpollEventLoop>();
}

}  // namespace atria::net

#endif  // __linux__
