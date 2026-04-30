#include "atria/application.hpp"
#include "atria/middleware.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/router.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using atria::Application;
using atria::Middleware;
using atria::Next;
using atria::Request;
using atria::Response;
using atria::Status;

TEST_CASE("middleware execute in registration order", "[middleware]") {
  Application app;
  std::vector<std::string> order;

  app.use([&](Request& req, const Next& next) {
    order.emplace_back("before-1");
    auto r = next(req);
    order.emplace_back("after-1");
    return r;
  });
  app.use([&](Request& req, const Next& next) {
    order.emplace_back("before-2");
    auto r = next(req);
    order.emplace_back("after-2");
    return r;
  });
  app.get("/", [&](Request&) {
    order.emplace_back("handler");
    return Response{Status::Ok};
  });

  Request request{atria::Method::Get, "/", "", {}, ""};
  Response r = app.dispatch(request);
  CHECK(r.status() == Status::Ok);
  REQUIRE(order.size() == 5);
  CHECK(order[0] == "before-1");
  CHECK(order[1] == "before-2");
  CHECK(order[2] == "handler");
  CHECK(order[3] == "after-2");
  CHECK(order[4] == "after-1");
}

TEST_CASE("middleware can short-circuit", "[middleware]") {
  Application app;
  bool handler_called = false;
  app.use([](Request&, const Next&) { return Response{Status::Forbidden}; });
  app.get("/", [&](Request&) {
    handler_called = true;
    return Response{Status::Ok};
  });

  Request request{atria::Method::Get, "/", "", {}, ""};
  Response r = app.dispatch(request);
  CHECK(r.status() == Status::Forbidden);
  CHECK_FALSE(handler_called);
}

TEST_CASE("dispatch returns 404 when no route", "[middleware]") {
  Application app;
  Request request{atria::Method::Get, "/missing", "", {}, ""};
  Response r = app.dispatch(request);
  CHECK(r.status() == Status::NotFound);
}

TEST_CASE("dispatch returns 405 when method not allowed", "[middleware]") {
  Application app;
  app.get("/items", [](Request&) { return Response{Status::Ok}; });
  Request request{atria::Method::Post, "/items", "", {}, ""};
  Response response = app.dispatch(request);
  CHECK(response.status() == Status::MethodNotAllowed);
}

TEST_CASE("group middleware composes inside global middleware", "[middleware][group]") {
  Application app;
  std::vector<std::string> order;

  app.use([&](Request& request, const atria::Next& next) {
    order.emplace_back("global-before");
    auto response = next(request);
    order.emplace_back("global-after");
    return response;
  });

  app.group("/api", [&](atria::RouteGroup& api) {
    api.use([&](Request& request, const atria::Next& next) {
      order.emplace_back("group-before");
      auto response = next(request);
      order.emplace_back("group-after");
      return response;
    });
    api.get("/items", [&](Request&) {
      order.emplace_back("handler");
      return Response{Status::Ok};
    });
  });

  Request request{atria::Method::Get, "/api/items", "", {}, ""};
  Response response = app.dispatch(request);
  CHECK(response.status() == Status::Ok);
  REQUIRE(order.size() == 5);
  CHECK(order[0] == "global-before");
  CHECK(order[1] == "group-before");
  CHECK(order[2] == "handler");
  CHECK(order[3] == "group-after");
  CHECK(order[4] == "global-after");
}

TEST_CASE("group middleware does not affect routes outside the group", "[middleware][group]") {
  Application app;
  bool group_middleware_ran = false;

  app.group("/api", [&](atria::RouteGroup& api) {
    api.use([&](Request& request, const atria::Next& next) {
      group_middleware_ran = true;
      return next(request);
    });
    api.get("/inside", [](Request&) { return Response{Status::Ok}; });
  });
  app.get("/outside", [](Request&) { return Response{Status::Ok}; });

  Request request{atria::Method::Get, "/outside", "", {}, ""};
  Response response = app.dispatch(request);
  CHECK(response.status() == Status::Ok);
  CHECK_FALSE(group_middleware_ran);
}

TEST_CASE("group middleware can short-circuit before the handler", "[middleware][group]") {
  Application app;
  bool handler_called = false;

  app.group("/api", [&](atria::RouteGroup& api) {
    api.use([](Request&, const atria::Next&) { return Response{Status::Forbidden}; });
    api.get("/items", [&](Request&) {
      handler_called = true;
      return Response{Status::Ok};
    });
  });

  Request request{atria::Method::Get, "/api/items", "", {}, ""};
  Response response = app.dispatch(request);
  CHECK(response.status() == Status::Forbidden);
  CHECK_FALSE(handler_called);
}

TEST_CASE("two groups have independent middleware", "[middleware][group]") {
  Application app;
  int api_count = 0;
  int admin_count = 0;

  app.group("/api", [&](atria::RouteGroup& api) {
    api.use([&](Request& request, const atria::Next& next) {
      ++api_count;
      return next(request);
    });
    api.get("/items", [](Request&) { return Response{Status::Ok}; });
  });
  app.group("/admin", [&](atria::RouteGroup& admin) {
    admin.use([&](Request& request, const atria::Next& next) {
      ++admin_count;
      return next(request);
    });
    admin.get("/users", [](Request&) { return Response{Status::Ok}; });
  });

  Request api_request{atria::Method::Get, "/api/items", "", {}, ""};
  (void)app.dispatch(api_request);
  Request admin_request{atria::Method::Get, "/admin/users", "", {}, ""};
  (void)app.dispatch(admin_request);

  CHECK(api_count == 1);
  CHECK(admin_count == 1);
}

TEST_CASE("group middleware registered after a route does not apply to it", "[middleware][group]") {
  // Registration order matters: middleware only wraps routes added afterwards. This is
  // the same rule as Express/Koa and matches user mental models.
  Application app;
  bool middleware_ran = false;

  app.group("/api", [&](atria::RouteGroup& api) {
    api.get("/early", [](Request&) { return Response{Status::Ok}; });
    api.use([&](Request& request, const atria::Next& next) {
      middleware_ran = true;
      return next(request);
    });
    api.get("/late", [](Request&) { return Response{Status::Ok}; });
  });

  Request early{atria::Method::Get, "/api/early", "", {}, ""};
  (void)app.dispatch(early);
  CHECK_FALSE(middleware_ran);

  Request late{atria::Method::Get, "/api/late", "", {}, ""};
  (void)app.dispatch(late);
  CHECK(middleware_ran);
}
