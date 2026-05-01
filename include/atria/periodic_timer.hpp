#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace atria {

class PeriodicTimer {
public:
  PeriodicTimer() = default;
  ~PeriodicTimer();

  PeriodicTimer(const PeriodicTimer&) = delete;
  PeriodicTimer& operator=(const PeriodicTimer&) = delete;
  PeriodicTimer(PeriodicTimer&&) = delete;
  PeriodicTimer& operator=(PeriodicTimer&&) = delete;

  void start(std::chrono::milliseconds interval, std::function<void()> callback);
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept { return running_.load(); }

private:
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace atria
