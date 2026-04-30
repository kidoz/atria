# atria-di-api

A small example wiring Atria with two companion libraries:

- **[CtorWire](https://github.com/kidoz/ctorwire)** — constructor-first DI for C++23. Services declare their constructor dependencies via the `ctorwire::dependencies<T>` trait; `ctorwire::make_injector(...)` composes the bindings.
- **[LogSpine](https://github.com/kidoz/logspine)** — structured logging with pluggable sinks. The example backs Atria's `ILogger` with a LogSpine `logger` resolved through CtorWire.

Both are pulled in as Meson git wraps under `subprojects/`.

## What it shows

| Service | How it's wired |
|---|---|
| `IClock` → `SystemClock` | `bind<IClock>().to<SystemClock>().as_singleton()` |
| `std::shared_ptr<logspine::logger>` | `instance<std::shared_ptr<logspine::logger>>(root_logger)` (created in `main`) |
| `ILogger` → `LogSpineLogger` | `bind<ILogger>().to<LogSpineLogger>().as_singleton()`; CtorWire resolves the LogSpine logger from the previous binding via the `dependencies<LogSpineLogger>` trait. |
| `GreetingService` | `injector.create<GreetingService>()` — its `dependencies<>` trait declares `IClock` and `ILogger` so CtorWire wires them automatically. |

## Build

```bash
meson setup builddir -Dcatch2:tests=false -Dctorwire:build_tests=false \
  -Dctorwire:build_examples=false -Dlogspine:build_tests=false -Dlogspine:build_examples=false
meson compile -C builddir
```

## Run

```bash
./builddir/examples/di_api/atria-di-api
# in another terminal:
curl -i localhost:8081/health
curl -i localhost:8081/hello/Ada
```

LogSpine writes structured records to the console:

```
2026-04-29T22:31:25.301Z info atria.di-api startup port=8081
[atria] listening on 0.0.0.0:8081
2026-04-29T22:31:25.514Z info atria.di-api request method=GET path=/health status=200
2026-04-29T22:31:25.525Z info atria.di-api greeting name=Ada ts=1777501885
2026-04-29T22:31:25.525Z info atria.di-api request method=GET path=/hello/Ada status=200
```

## Notes

- The example uses `logspine::sync_dispatcher` so each call writes immediately. Production deployments should prefer `async_dispatcher` with a bounded queue (the LogSpine README documents the options).
- CtorWire is header-only; LogSpine builds a small static/shared library. Both stay outside Atria's core dependency graph — they only show up if you opt into this example.

## See also

For a full list of companion C++23 libraries that pair naturally with Atria — including [AsterORM](https://github.com/kidoz/asterorm) for PostgreSQL persistence — see `docs/companion-libraries.md`. A future `examples/postgres_api/` will demonstrate AsterORM running on the worker pool.
