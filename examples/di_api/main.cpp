// Atria + CtorWire + LogSpine example
//
// Demonstrates constructor-first dependency injection (https://github.com/kidoz/ctorwire)
// composing an Atria HTTP application whose ILogger is backed by structured logging from
// LogSpine (https://github.com/kidoz/logspine). Wiring is declared once at startup; handlers
// receive their dependencies through the injector with no global state.

#include <atria/atria.hpp>
#include <ctorwire/ctorwire.hpp>
#include <logspine/logspine.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct IClock {
  virtual ~IClock() = default;
  [[nodiscard]] virtual std::int64_t now_unix_seconds() const = 0;
};

struct SystemClock final : IClock {
  [[nodiscard]] std::int64_t now_unix_seconds() const override {
    using clock = std::chrono::system_clock;
    return std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch())
        .count();
  }
};

// Atria-side logger interface. Field type is exposed because the example deliberately
// integrates with LogSpine's structured fields; you could also wrap them in your own type.
struct ILogger {
  virtual ~ILogger() = default;
  virtual void info(std::string_view message, std::vector<logspine::field> fields = {}) = 0;
  virtual void warn(std::string_view message, std::vector<logspine::field> fields = {}) = 0;
  virtual void error(std::string_view message, std::vector<logspine::field> fields = {}) = 0;
};

// LogSpine-backed adapter. Receives a registered LogSpine logger via constructor — wired
// through CtorWire as a `shared_ptr<logspine::logger>` instance binding.
class LogSpineLogger final : public ILogger {
 public:
  explicit LogSpineLogger(std::shared_ptr<logspine::logger> log) : log_(std::move(log)) {}

  void info(std::string_view message, std::vector<logspine::field> fields) override {
    log_->log(logspine::level::info, message, std::move(fields));
    log_->flush();
  }
  void warn(std::string_view message, std::vector<logspine::field> fields) override {
    log_->log(logspine::level::warn, message, std::move(fields));
    log_->flush();
  }
  void error(std::string_view message, std::vector<logspine::field> fields) override {
    log_->log(logspine::level::error, message, std::move(fields));
    log_->flush();
  }

 private:
  std::shared_ptr<logspine::logger> log_;
};

class GreetingService {
 public:
  GreetingService(std::shared_ptr<IClock> clock, std::shared_ptr<ILogger> logger)
      : clock_(std::move(clock)), logger_(std::move(logger)) {}

  atria::Json greet(std::string_view name) {
    const auto ts = clock_->now_unix_seconds();
    logger_->info("greeting", {
                                  logspine::kv("name", std::string{name}),
                                  logspine::kv("ts", ts),
                              });
    return atria::Json::object({
        {"hello", std::string{name}},
        {"at", ts},
    });
  }

 private:
  std::shared_ptr<IClock> clock_;
  std::shared_ptr<ILogger> logger_;
};

// Build a registered LogSpine logger backed by a console sink. Uses `sync_dispatcher` so
// each call writes immediately — convenient for a demo. Switch to `async_dispatcher` for
// production workloads that need a bounded queue and batching.
[[nodiscard]] std::shared_ptr<logspine::logger_registry> build_logspine_registry() {
  auto console = std::make_shared<logspine::sinks::console_sink>(
      logspine::sinks::console_sink_options{.format = logspine::sink_format::human});
  auto dispatcher = std::make_shared<logspine::sync_dispatcher>(
      std::vector<std::shared_ptr<logspine::sink>>{console});
  return std::make_shared<logspine::logger_registry>(dispatcher, logspine::level::debug);
}

}  // namespace

template <>
struct ctorwire::dependencies<LogSpineLogger> {
  using type = ctorwire::types<std::shared_ptr<logspine::logger>>;
};

template <>
struct ctorwire::dependencies<GreetingService> {
  using type = ctorwire::types<std::shared_ptr<IClock>, std::shared_ptr<ILogger>>;
};

int main() {
  auto registry = build_logspine_registry();
  auto root_logger = registry->get("atria.di-api");

  auto injector = ctorwire::make_injector(
      ctorwire::bind<IClock>().to<SystemClock>().as_singleton(),
      ctorwire::instance<std::shared_ptr<logspine::logger>>(root_logger),
      ctorwire::bind<ILogger>().to<LogSpineLogger>().as_singleton());

  auto logger = injector.resolve<std::shared_ptr<ILogger>>();
  auto greeting = std::make_shared<GreetingService>(injector.create<GreetingService>());

  atria::Application app;
  app.use(atria::middleware::error_handler());

  app.use([logger](atria::Request& req, const atria::Next& next) {
    auto response = next(req);
    logger->info("request",
                 {
                     logspine::kv("method", std::string{atria::to_string(req.method())}),
                     logspine::kv("path", std::string{req.path()}),
                     logspine::kv("status", static_cast<std::int64_t>(
                                                static_cast<std::uint16_t>(response.status()))),
                 });
    return response;
  });

  app.get("/health", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"status", "ok"}}));
  });

  app.get("/hello/{name}", [greeting](atria::Request& request) {
    auto name = request.path_param("name").value_or("world");
    return atria::Response::json(greeting->greet(name));
  });

  logger->info("startup", {logspine::kv("port", std::int64_t{8081})});
  atria::ServerConfig cfg;
  cfg.host = "0.0.0.0";
  cfg.port = 8081;
  cfg.worker_threads = 4;
  int rc = app.listen(cfg);
  registry->flush();
  return rc;
}
