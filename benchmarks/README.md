# Atria benchmarks

Tiny self-contained microbenchmarks for the router, JSON parser, and HTTP parser. Disabled by default — opt in with the Meson option:

```bash
meson setup build-bench --buildtype=release -Dbenchmarks=true \
  -Dcatch2:tests=false -Dctorwire:build_tests=false -Dctorwire:build_examples=false \
  -Dlogspine:build_tests=false -Dlogspine:build_examples=false
meson compile -C build-bench
```

Run them individually:

```bash
./build-bench/benchmarks/atria-bench-router
./build-bench/benchmarks/atria-bench-json
./build-bench/benchmarks/atria-bench-parser
```

Each binary prints `ns/op` and `ops/s` per workload. The harness lives in `benchmarks/timer.hpp` — it does a 10% warmup and a fixed iteration count. It is not a replacement for nanobench/Catch2-bench; it is enough to catch order-of-magnitude regressions during development.

## Caveats

- These are **microbenchmarks** — they isolate one component, not full request flow.
- For an end-to-end performance signal, run `atria-todo-api` and use `wrk` / `oha` / `bombardier` against `localhost:8080`.
- Always benchmark a release build (`--buildtype=release`). Debug builds and sanitizer builds are 2–10× slower and should never be used for performance comparisons.
