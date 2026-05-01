#include "atria/periodic_timer.hpp"

#include <chrono>
#include <functional>
#include <thread>
#include <utility>

namespace atria {

PeriodicTimer::~PeriodicTimer() {
  stop();
}

void PeriodicTimer::start(std::chrono::milliseconds interval, std::function<void()> callback) {
  stop();
  if (interval.count() <= 0) {
    interval = std::chrono::milliseconds{1};
  }
  running_.store(true);
  thread_ = std::thread{[this, interval, callback = std::move(callback)] {
    while (running_.load()) {
      std::this_thread::sleep_for(interval);
      if (running_.load()) {
        callback();
      }
    }
  }};
}

void PeriodicTimer::stop() noexcept {
  running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }
}

}  // namespace atria
