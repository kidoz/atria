#include "atria/method.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/router.hpp"

#include <catch2/catch_test_macros.hpp>

using atria::Handler;
using atria::MatchOutcome;
using atria::Method;
using atria::Request;
using atria::Response;
using atria::Router;
using atria::Status;

namespace {

Handler ok_handler() {
  return [](Request&) { return Response{Status::Ok}; };
}

}  // namespace

TEST_CASE("matches static routes", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/health", ok_handler()).has_value());

  auto m = r.match(Method::Get, "/health");
  CHECK(m.outcome == MatchOutcome::Found);
}

TEST_CASE("matches path parameters", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/users/{id}", ok_handler()).has_value());

  auto m = r.match(Method::Get, "/users/42");
  REQUIRE(m.outcome == MatchOutcome::Found);
  REQUIRE(m.params.size() == 1);
  CHECK(m.params[0].first == "id");
  CHECK(m.params[0].second == "42");
}

TEST_CASE("literal routes win over parameter routes", "[router]") {
  Router r;
  bool literal_called = false;
  bool param_called = false;
  REQUIRE(r.add(Method::Get, "/users/me", [&](Request&) {
                literal_called = true;
                return Response{Status::Ok};
              }).has_value());
  REQUIRE(r.add(Method::Get, "/users/{id}", [&](Request&) {
                param_called = true;
                return Response{Status::Ok};
              }).has_value());
  auto m = r.match(Method::Get, "/users/me");
  REQUIRE(m.outcome == MatchOutcome::Found);
  Request req;
  m.handler(req);
  CHECK(literal_called);
  CHECK_FALSE(param_called);
}

TEST_CASE("returns 404 for unknown route", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/items", ok_handler()).has_value());
  auto m = r.match(Method::Get, "/missing");
  CHECK(m.outcome == MatchOutcome::NotFound);
}

TEST_CASE("returns 405 when path matches but method does not", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/items", ok_handler()).has_value());
  auto m = r.match(Method::Post, "/items");
  CHECK(m.outcome == MatchOutcome::MethodNotAllowed);
}

TEST_CASE("rejects invalid route registration", "[router]") {
  Router r;
  CHECK_FALSE(r.add(Method::Get, "no-leading-slash", ok_handler()).has_value());
  CHECK_FALSE(r.add(Method::Get, "/users/{}", ok_handler()).has_value());
  CHECK_FALSE(r.add(Method::Get, "/users/{id", ok_handler()).has_value());
  CHECK_FALSE(r.add(Method::Get, "/{id}/{id}", ok_handler()).has_value());
}

TEST_CASE("route group prefixing works", "[router]") {
  Router router;
  atria::RouteGroup api_group{router, "/api/v1"};
  api_group.get("/items", ok_handler());
  api_group.post("/items", ok_handler());

  CHECK(router.match(Method::Get, "/api/v1/items").outcome == MatchOutcome::Found);
  CHECK(router.match(Method::Post, "/api/v1/items").outcome == MatchOutcome::Found);
}
