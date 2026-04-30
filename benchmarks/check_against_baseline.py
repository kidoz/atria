#!/usr/bin/env python3
"""Run Atria's microbenchmarks and check each measurement against benchmarks/baseline.json.

Usage:
    python3 benchmarks/check_against_baseline.py <build-dir>

The script:
  * runs each benchmark binary under <build-dir>/benchmarks/
  * parses the "label  iters  ns/op  ops/s" lines they print
  * looks up the baseline ns/op for each label
  * fails (non-zero exit) when any measurement exceeds baseline * tolerance_multiplier
  * warns about labels missing from the baseline (so stale baselines surface)
  * warns about benchmarks listed in the baseline that didn't appear in the output

Tolerance is loose by design — microbenchmarks on CI runners (especially shared-tenant
GitHub Actions runners) are noisy. The goal is to catch order-of-magnitude regressions,
not micro-optimizations.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

# Each "label  N iters  X.X ns/op  Y ops/s" benchmark line.
LINE_PATTERN = re.compile(
    r"^(?P<label>\S.*?)\s{2,}\d+\s+iters\s+(?P<ns>[\d.]+)\s+ns/op\s+\d+\s+ops/s\s*$"
)

BENCH_BINARIES = (
    "atria-bench-router",
    "atria-bench-json",
    "atria-bench-parser",
)


def run_binary(path: Path) -> dict[str, float]:
    """Execute a benchmark binary and return {label: measured_ns_per_op}."""
    result = subprocess.run(
        [str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    measurements: dict[str, float] = {}
    for line in result.stdout.splitlines():
        match = LINE_PATTERN.match(line)
        if match is None:
            continue
        measurements[match["label"].strip()] = float(match["ns"])
    return measurements


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {argv[0]} <build-dir>", file=sys.stderr)
        return 2
    build_dir = Path(argv[1]).resolve()
    benchmarks_dir = build_dir / "benchmarks"
    if not benchmarks_dir.exists():
        print(f"no benchmarks/ subdir under {build_dir}", file=sys.stderr)
        return 2

    repo_root = Path(__file__).resolve().parent.parent
    baseline_path = repo_root / "benchmarks" / "baseline.json"
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    tolerance = float(baseline.get("tolerance_multiplier", 3.0))
    baseline_table = baseline["benchmarks"]

    failures: list[str] = []
    warnings: list[str] = []

    print(f"Tolerance: {tolerance}x (measured must be <= baseline * tolerance)\n")

    for binary_name in BENCH_BINARIES:
        binary_path = benchmarks_dir / binary_name
        if not binary_path.exists():
            warnings.append(f"missing binary: {binary_path}")
            continue
        print(f"=== {binary_name} ===")
        measurements = run_binary(binary_path)
        baseline_for_binary = baseline_table.get(binary_name, {})

        for label, measured in measurements.items():
            baseline_value = baseline_for_binary.get(label)
            if baseline_value is None:
                warnings.append(
                    f"{binary_name}/{label!r}: not in baseline (add it or rename mismatch)"
                )
                continue
            limit = baseline_value * tolerance
            status = "OK" if measured <= limit else "FAIL"
            print(
                f"  [{status}] {label}: {measured:.1f} ns/op "
                f"(baseline {baseline_value:.1f}, limit {limit:.1f})"
            )
            if measured > limit:
                failures.append(
                    f"{binary_name}/{label!r}: {measured:.1f} ns/op > {limit:.1f} ns/op"
                )

        for label in baseline_for_binary:
            if label not in measurements:
                warnings.append(
                    f"{binary_name}/{label!r}: in baseline but not measured (renamed/removed?)"
                )
        print()

    if warnings:
        print("Warnings:")
        for warning in warnings:
            print(f"  - {warning}")
        print()

    if failures:
        print("Regressions detected:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("All benchmarks within tolerance.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
