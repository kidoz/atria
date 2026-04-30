// kqueue event loop for macOS and BSD. Level-triggered.

#include "net/event_loop.hpp"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)

#include "platform/socket.hpp"

#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atria::net {

namespace {

class KqueueEventLoop final : public EventLoop {
 public:
  KqueueEventLoop() : kq_(::kqueue()) {}

  ~KqueueEventLoop() override {
    if (kq_ >= 0) {
      ::close(kq_);
    }
  }

  void watch(platform::NativeSocket fd, IoEvent events, IoCallback cb) override {
    callbacks_[fd] = std::move(cb);
    apply(fd, IoEvent::None, events);
    interest_[fd] = events;
  }

  void modify(platform::NativeSocket fd, IoEvent events) override {
    auto it = interest_.find(fd);
    IoEvent current = (it != interest_.end()) ? it->second : IoEvent::None;
    apply(fd, current, events);
    interest_[fd] = events;
  }

  void unwatch(platform::NativeSocket fd) override {
    auto it = interest_.find(fd);
    if (it != interest_.end()) {
      apply(fd, it->second, IoEvent::None);
      interest_.erase(it);
    }
    callbacks_.erase(fd);
  }

  int run() override {
    if (kq_ < 0) {
      return 1;
    }
    stop_.store(false);
    while (!stop_.load() && !interest_.empty()) {
      run_once(200);
    }
    return 0;
  }

  void run_once(int timeout_ms) override {
    if (kq_ < 0 || interest_.empty()) {
      return;
    }
    constexpr int kBatch = 64;
    std::vector<struct kevent> events(kBatch);
    timespec ts{};
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
    int n = ::kevent(kq_, nullptr, 0, events.data(), kBatch, &ts);
    if (n <= 0) {
      return;
    }
    std::vector<std::pair<platform::NativeSocket, IoEvent>> ready;
    auto count = static_cast<std::size_t>(n);
    ready.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      auto fd = static_cast<platform::NativeSocket>(events[i].ident);
      IoEvent ev = IoEvent::None;
      if (events[i].filter == EVFILT_READ) {
        ev = IoEvent::Read;
      } else if (events[i].filter == EVFILT_WRITE) {
        ev = IoEvent::Write;
      }
      if ((events[i].flags & EV_ERROR) != 0 || (events[i].flags & EV_EOF) != 0) {
        ev = ev | IoEvent::Read;
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
  void apply(platform::NativeSocket fd, IoEvent before, IoEvent after) {
    auto change = [&](short filter, bool was, bool now) {
      if (was == now) {
        return;
      }
      struct kevent ev {};
      EV_SET(&ev, static_cast<uintptr_t>(fd), filter, now ? EV_ADD : EV_DELETE, 0, 0, nullptr);
      (void)::kevent(kq_, &ev, 1, nullptr, 0, nullptr);
    };
    change(EVFILT_READ, any(before & IoEvent::Read), any(after & IoEvent::Read));
    change(EVFILT_WRITE, any(before & IoEvent::Write), any(after & IoEvent::Write));
  }

  int kq_;
  std::unordered_map<platform::NativeSocket, IoCallback> callbacks_;
  std::unordered_map<platform::NativeSocket, IoEvent> interest_;
  std::atomic<bool> stop_{false};
};

}  // namespace

std::unique_ptr<EventLoop> make_event_loop() {
  return std::make_unique<KqueueEventLoop>();
}

}  // namespace atria::net

#endif  // BSD/macOS
