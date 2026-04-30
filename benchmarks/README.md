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

## Regression check

`benchmarks/baseline.json` records expected ns/op for each labelled workload, and `benchmarks/check_against_baseline.py` runs every binary and asserts each measurement is within `tolerance_multiplier × baseline`. Tolerance is intentionally loose (5×) because CI runners are 2–4× slower and noisier than developer machines — the goal is to catch order-of-magnitude regressions, not micro-tuning.

```bash
python3 benchmarks/check_against_baseline.py build-bench
```

The script:

- runs each `atria-bench-*` binary,
- parses lines matching `<label>  <iters> iters  <ns> ns/op  <ops> ops/s`,
- compares against `baseline.json`,
- exits non-zero if any measurement exceeds the tolerance,
- warns about labels that appear in only one of the two (drift signal — re-baseline or rename).

CI runs this script as a separate job (`benchmarks` in `.github/workflows/ci.yml`).

## Re-baselining

When you change the benchmarks deliberately (new workload, faster algorithm, intentional perf change):

1. Run all three binaries on a release build on a quiet developer machine.
2. Edit `benchmarks/baseline.json` to reflect the new expected ns/op.
3. Commit `baseline.json` alongside the change so reviewers see the perf delta.

Don't quietly raise the baseline to silence a CI failure — that defeats the regression check. If a test machine is slow enough to fail, raise `tolerance_multiplier` instead, or fix the underlying regression.

## Caveats

- These are **microbenchmarks** — they isolate one component, not full request flow.
- For an end-to-end performance signal, run `atria-todo-api` and use `wrk` / `oha` / `bombardier` against `localhost:8080`.
- Always benchmark a release build (`--buildtype=release`). Debug builds and sanitizer builds are 2–10× slower and should never be used for performance comparisons.
