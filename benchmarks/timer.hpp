// Tiny self-contained microbenchmark harness. Not a replacement for nanobench/Catch2-bench,
// but enough for smoke-level "did this regress by an order of magnitude?" signal.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace atria_bench {

template <typename F>
void run(std::string_view label, std::size_t iterations, F&& body) {
  // Warmup
  for (std::size_t i = 0; i < iterations / 10; ++i) {
    body();
  }

  using clock = std::chrono::steady_clock;
  auto start = clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    body();
  }
  auto end = clock::now();

  auto ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  double per_op = static_cast<double>(ns) / static_cast<double>(iterations);
  double ops_per_sec = (per_op > 0.0) ? 1e9 / per_op : 0.0;

  std::string padded{label};
  std::printf("%-40s  %10zu iters  %10.1f ns/op  %12.0f ops/s\n", padded.c_str(), iterations,
              per_op, ops_per_sec);
}

}  // namespace atria_bench
