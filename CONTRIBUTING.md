# Contributing to Atria

Thank you for considering a contribution to Atria.

## Ground rules

- Atria is a new C++23 REST framework built **from scratch**. Pull requests must not introduce a dependency on Drogon, Crow, RESTinio, Pistache, Boost.Beast, Boost.Asio, cpp-httplib, Qt Network, POCO Net, or any other web/networking framework.
- The library targets Linux, macOS, and Windows. Platform-specific code must stay under `src/platform/<posix|windows>/`.
- Warnings are errors. clang-tidy and clang-format must pass.

## Building

```bash
meson setup builddir
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Sanitizer build:

```bash
CC=clang CXX=clang++ meson setup build-asan \
  -Db_sanitize=address,undefined \
  -Db_lundef=false
meson compile -C build-asan
meson test -C build-asan --print-errorlogs
```

## Style

Follow the project `.clang-format` (LLVM-like). Run:

```bash
find include src tests examples -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
```

## Tests

Every new public API or bug fix needs:

- a unit test in `tests/`
- coverage of the success and at least one failure path

Tests use Catch2 v3 via Meson WrapDB.

## Commit messages

Use short, imperative summaries:

```
http: reject duplicate Content-Length headers
router: literal segments win over parameter segments
```

## Reporting security issues

See `SECURITY.md`.
