# Atria fuzz harnesses

Fuzz harnesses for parser and routing trust boundaries. They are disabled by default and require Clang. Meson links the real libFuzzer main when available; otherwise it builds the same entry points with a standalone corpus driver for smoke testing.

```bash
CC=clang CXX=clang++ meson setup build-fuzz -Dfuzz=true \
  -Dcatch2:tests=false -Dctorwire:build_tests=false \
  -Dctorwire:build_examples=false -Dlogspine:build_tests=false \
  -Dlogspine:build_examples=false
meson compile -C build-fuzz \
  atria-fuzz-http-parser \
  atria-fuzz-url-path \
  atria-fuzz-router \
  atria-fuzz-websocket-frame
```

Smoke run with the committed seed corpus:

```bash
build-fuzz/tests/fuzz/atria-fuzz-http-parser tests/fuzz/corpus/http-parser -runs=64
build-fuzz/tests/fuzz/atria-fuzz-url-path tests/fuzz/corpus/url-path -runs=64
build-fuzz/tests/fuzz/atria-fuzz-router tests/fuzz/corpus/router -runs=64
build-fuzz/tests/fuzz/atria-fuzz-websocket-frame tests/fuzz/corpus/websocket-frame -runs=64
```

Longer local runs should use a writable corpus directory and an artifact directory:

```bash
mkdir -p build-fuzz/corpus/http-parser build-fuzz/artifacts
cp tests/fuzz/corpus/http-parser/* build-fuzz/corpus/http-parser/
build-fuzz/tests/fuzz/atria-fuzz-http-parser build-fuzz/corpus/http-parser \
  -artifact_prefix=build-fuzz/artifacts/
```
