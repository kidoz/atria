#include "net/worker_pool.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace atria::net {

WorkerPool::WorkerPool(std::size_t threads) {
  workers_.reserve(threads);
  for (std::size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this] { run_worker(); });
  }
}

WorkerPool::~WorkerPool() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_.store(true);
  }
  cv_.notify_all();
  for (auto& t : workers_) {
    if (t.joinable()) {
      t.join();
    }
  }
}

void WorkerPool::submit(std::function<void()> job) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    jobs_.push(std::move(job));
  }
  cv_.notify_one();
}

void WorkerPool::run_worker() {
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] { return stop_.load() || !jobs_.empty(); });
      if (stop_.load() && jobs_.empty()) {
        return;
      }
      job = std::move(jobs_.front());
      jobs_.pop();
    }
    if (job) {
      job();
    }
  }
}

}  // namespace atria::net
