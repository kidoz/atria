// Fixed-size thread pool for offloading slow handlers.
//
// Each worker thread pulls a job (std::function<void()>) from a shared queue and runs it.
// Used by the runtime to dispatch HTTP handlers off the event-loop thread; the handler's
// completion is signalled back to the loop via a Notifier + completion queue (set up by
// the runtime, not this class).

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace atria::net {

class WorkerPool {
 public:
  explicit WorkerPool(std::size_t threads);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;
  WorkerPool(WorkerPool&&) = delete;
  WorkerPool& operator=(WorkerPool&&) = delete;

  void submit(std::function<void()> job);

  [[nodiscard]] std::size_t size() const noexcept { return workers_.size(); }

 private:
  void run_worker();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> jobs_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<bool> stop_{false};
};

}  // namespace atria::net
