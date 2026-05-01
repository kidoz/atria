#pragma once

#include "atria/json.hpp"
#include "atria/middleware.hpp"
#include "atria/router.hpp"
#include "atria/server_config.hpp"
#include "atria/websocket.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace atria {

class Application {
public:
  Application();
  ~Application();
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  Application& use(Middleware middleware);

  // Verb registration. Returns a RouteBuilder so callers can attach OpenAPI metadata:
  //   app.get("/health", handler).name("getHealth").summary("Liveness check");
  RouteBuilder get(std::string_view path, Handler handler);
  RouteBuilder post(std::string_view path, Handler handler);
  RouteBuilder put(std::string_view path, Handler handler);
  RouteBuilder patch(std::string_view path, Handler handler);
  RouteBuilder del(std::string_view path, Handler handler);
  RouteBuilder options(std::string_view path, Handler handler);
  RouteBuilder head(std::string_view path, Handler handler);
  RouteBuilder subscribe(std::string_view path, Handler handler);
  RouteBuilder unsubscribe(std::string_view path, Handler handler);
  RouteBuilder notify(std::string_view path, Handler handler);
  RouteBuilder websocket(std::string_view path, WebSocketHandler handler);

  Application& group(std::string_view prefix, const std::function<void(RouteGroup&)>& builder);

  [[nodiscard]] Response dispatch(Request& request);
  [[nodiscard]] bool dispatch_websocket(Request& request, WebSocketSession& session);

  int listen(ServerConfig config);
  void shutdown() noexcept;

  [[nodiscard]] Router& router() noexcept { return router_; }

  [[nodiscard]] const Router& router() const noexcept { return router_; }

  // OpenAPI metadata configuration. Set the API's display name and version; the values
  // appear under `info.title` / `info.version` in the emitted document.
  Application& info(std::string title, std::string version);

  // Emit an OpenAPI 3.0.3 JSON document describing every registered route. Routes with
  // no metadata fields set still appear (with just the path + method); call
  // `RouteBuilder::name()` etc. on them to populate richer fields.
  [[nodiscard]] Json openapi_json() const;

private:
  struct WebSocketRoute {
    std::string path;
    WebSocketHandler handler;
    RouteMeta meta;
  };

  Router router_;
  std::vector<std::unique_ptr<WebSocketRoute>> websocket_routes_;
  std::vector<Middleware> middleware_;
  std::atomic<bool> running_{false};
  std::string openapi_title_{"Atria API"};
  std::string openapi_version_{"0.1.0"};
};

}  // namespace atria
