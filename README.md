# Atria

A new C++23 REST web framework, built **from scratch**. Inspired by FastAPI, ASP.NET MVC, and Spring Web.

[![ci](https://img.shields.io/badge/ci-pending-lightgrey.svg)](.github/workflows/ci.yml)
[![status](https://img.shields.io/badge/status-experimental-orange.svg)](#roadmap)
[![cpp](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](#)

## Quick example

```cpp
#include <atria/atria.hpp>

int main() {
  atria::Application app;

  app.use(atria::middleware::request_logger());

  app.get("/health", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"status", "ok"}}));
  });

  app.group("/api/v1", [](atria::RouteGroup& api) {
    api.get("/hello/{name}", [](atria::Request& request) {
      auto name = request.path_param("name").value_or("world");
      return atria::Response::json(atria::Json::object({{"hello", std::string{name}}}));
    });
  });

  atria::ServerConfig cfg;
  cfg.host = "0.0.0.0";
  cfg.port = 8080;
  cfg.worker_threads = 4;  // dispatch handlers off the I/O loop
  return app.listen(cfg);
}
```

## Features

- HTTP/1.1 request parser with explicit limits (request line, header bytes, header count, body bytes)
- Keep-alive, chunked request decoding, idle/read/write timeouts
- Router with static routes, `{name}` path parameters, route groups, and 404/405 outcomes
- Middleware pipeline (`request_logger`, `cors`, `error_handler` built-ins included)
- `atria::Json` value type with parser, serializer, depth/length limits
- DTO-style validation helpers and a consistent JSON error response shape
- **Streaming responses** (`Response::stream(provider)`) for file downloads, large exports, server-sent events
- Event-driven runtime: epoll on Linux, kqueue on macOS/BSD, poll/WSAPoll elsewhere; optional worker pool for slow handlers (`ServerConfig::worker_threads`)
- Cross-platform TCP runtime (POSIX sockets, Winsock) behind an internal abstraction
- Meson build with warnings-as-errors, clang-format, clang-tidy, and sanitizer-friendly options

## Build requirements

- C++23 compiler (Clang 17+, GCC 13+, MSVC 19.36+)
- Meson 1.3+
- Ninja
- pkg-config (Linux/macOS, optional but recommended)

## Build commands

```bash
meson setup builddir -Dcatch2:tests=false
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Sanitizer build (Linux/macOS):

```bash
CC=clang CXX=clang++ meson setup build-asan \
  -Db_sanitize=address,undefined \
  -Db_lundef=false \
  -Dcatch2:tests=false
meson compile -C build-asan
meson test -C build-asan --print-errorlogs
```

## Todo API example

`examples/todo_api/main.cpp` builds the `atria-todo-api` binary, a small CRUD service:

```bash
./builddir/examples/todo_api/atria-todo-api
# in another terminal
curl localhost:8080/health
curl -X POST -H 'content-type: application/json' \
  -d '{"name":"buy milk"}' localhost:8080/api/v1/items
curl localhost:8080/api/v1/items
```

## Framework architecture

```text
include/atria/                Public headers (Application, Request, Response, Router, Json, …)
src/application/              Application façade and dispatch
src/http/                     HTTP/1.1 parser, headers, request, response, status, url
src/routing/                  Route trie, RouteGroup, path-param extraction
src/middleware/               Built-in middleware
src/json/                     Json value, parser, serializer
src/validation/               Validator, ValidationError, error JSON shape
src/net/                      TcpListener, ServerRuntime
src/platform/{posix,windows}/ Platform sockets
tests/                        Catch2 v3 unit tests
examples/todo_api/            Todo CRUD example
```

## Safety and code quality

- C++23 with `-Werror`, `-Wconversion`, `-Wshadow`, `-Wsign-conversion`, and friends
- clang-tidy enabled (`bugprone-*`, `cert-*`, `cppcoreguidelines-*`, `performance-*`, `readability-*`, …)
- `std::expected<T, E>` is the default error model for parser and runtime APIs
- Request parser enforces hard limits and rejects malformed input with appropriate HTTP statuses
- Response serializer rejects CRLF in header names/values
- Cross-platform TCP runtime uses RAII socket handles

## Roadmap

- [ ] Group-scoped middleware
- [ ] Keep-alive
- [ ] Chunked transfer encoding
- [ ] OpenAPI export driven from route metadata
- [ ] epoll / kqueue / IOCP runtimes
- [ ] TLS termination
- [ ] DI / service registration

## Contributing

See `CONTRIBUTING.md`.

## License

See `LICENSE`. Atria currently ships with a placeholder while the maintainers finalize licensing.
