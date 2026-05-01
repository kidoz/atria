#pragma once

#include "atria/method.hpp"
#include "atria/middleware.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/route_builder.hpp"
#include "atria/route_meta.hpp"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atria {

struct RouteError {
  std::string message;
};

enum class MatchOutcome : std::uint8_t {
  Found,
  NotFound,
  MethodNotAllowed,
};

struct MatchResult {
  MatchOutcome outcome{MatchOutcome::NotFound};
  Handler handler;
  Request::PathParams params;
};

class Router {
public:
  Router();
  ~Router();
  Router(const Router&) = delete;
  Router& operator=(const Router&) = delete;
  Router(Router&&) noexcept;
  Router& operator=(Router&&) noexcept;

  // Register a route. On success, returns a pointer to the metadata slot for the route
  // (owned by the router) so callers can attach OpenAPI metadata via RouteBuilder.
  std::expected<RouteMeta*, RouteError> add(Method method, std::string_view path, Handler handler);

  [[nodiscard]] MatchResult match(Method method, std::string_view path) const;

  // Iterate over every registered route; each callback receives the metadata for one
  // (method, path) pair. Used by OpenAPI emission. Visitation order is unspecified but
  // stable across calls if no routes are added or removed in between.
  void for_each_route(const std::function<void(const RouteMeta&)>& visitor) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class RouteGroup {
public:
  RouteGroup(Router& router, std::string prefix);

  // Add middleware that wraps every handler registered through this group. Middleware
  // added before route registration is composed around routes registered after — order
  // matters. The middleware chain applied at dispatch time is:
  //
  //     global-mw (outer) → group-mw → handler
  //
  // ...so adding `auth_middleware()` to a `/api/v1` group makes /api/v1/* routes require
  // auth without affecting /health.
  RouteGroup& use(Middleware middleware);

  // Verb registration. Returns a RouteBuilder writing into the route's metadata slot so
  // callers can attach OpenAPI fields fluently:
  //   api.get("/items", list_items).name("listItems").summary("List all items");
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

private:
  RouteMeta* add(Method method, std::string_view path, Handler handler);
  [[nodiscard]] Handler wrap_with_group_middleware(Handler handler) const;

  Router& router_;
  std::string prefix_;
  std::vector<Middleware> middleware_;
};

}  // namespace atria
