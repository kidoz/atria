#include "atria/periodic_timer.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

TEST_CASE("PeriodicTimer runs and stops correctly", "[periodic_timer]") {
  atria::PeriodicTimer timer;
  std::atomic<int> counter{0};

  timer.start(std::chrono::milliseconds(10), [&counter]() {
    counter++;
  });

  REQUIRE(timer.running());

  // Wait long enough for a few ticks
  std::this_thread::sleep_for(std::chrono::milliseconds(55));

  timer.stop();
  REQUIRE_FALSE(timer.running());

  int current_count = counter.load();
  CHECK(current_count >= 3);
  CHECK(current_count <= 8); // Allow some slack due to OS scheduling

  // Wait a bit more to ensure it stopped
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(counter.load() == current_count);
}
