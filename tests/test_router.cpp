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

TEST_CASE("decodes percent-encoded path parameters", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/files/{name}", ok_handler()).has_value());

  auto m = r.match(Method::Get, "/files/hello%20world");
  REQUIRE(m.outcome == MatchOutcome::Found);
  REQUIRE(m.params.size() == 1);
  CHECK(m.params[0].first == "name");
  CHECK(m.params[0].second == "hello world");
}

TEST_CASE("decodes percent-encoded literal path segments", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/files/report", ok_handler()).has_value());

  auto m = r.match(Method::Get, "/files/%72eport");
  CHECK(m.outcome == MatchOutcome::Found);
}

TEST_CASE("does not decode plus as space in path parameters", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/math/{expr}", ok_handler()).has_value());

  auto m = r.match(Method::Get, "/math/1+1");
  REQUIRE(m.outcome == MatchOutcome::Found);
  REQUIRE(m.params.size() == 1);
  CHECK(m.params[0].first == "expr");
  CHECK(m.params[0].second == "1+1");
}

TEST_CASE("rejects invalid percent escapes in path segments", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/files/{name}", ok_handler()).has_value());

  CHECK(r.match(Method::Get, "/files/%XX").outcome == MatchOutcome::NotFound);
  CHECK(r.match(Method::Get, "/files/%2").outcome == MatchOutcome::NotFound);
}

TEST_CASE("normalizes dot segments preventing path traversal", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/items", ok_handler()).has_value());
  REQUIRE(r.add(Method::Get, "/", ok_handler()).has_value());

  CHECK(r.match(Method::Get, "/api/../items").outcome == MatchOutcome::Found);
  CHECK(r.match(Method::Get, "/items/./").outcome == MatchOutcome::Found);
  CHECK(r.match(Method::Get, "/items/..").outcome == MatchOutcome::Found);
  CHECK(r.match(Method::Get, "/items/../../..").outcome == MatchOutcome::Found);
  CHECK(r.match(Method::Get, "/api/%2E%2E/items").outcome == MatchOutcome::Found);
}

TEST_CASE("rejects encoded path separators in route segments", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/files/{path}", ok_handler()).has_value());

  CHECK(r.match(Method::Get, "/files/a%2Fb").outcome == MatchOutcome::NotFound);
  CHECK(r.match(Method::Get, "/files/a%5Cb").outcome == MatchOutcome::NotFound);
  CHECK(r.match(Method::Get, "/files/a%2F..%2Fsecret").outcome == MatchOutcome::NotFound);
}

TEST_CASE("normalizes encoded traversal before route matching", "[router]") {
  Router r;
  REQUIRE(r.add(Method::Get, "/files/{path}", ok_handler()).has_value());

  auto m = r.match(Method::Get, "/files/%2E%2E/files/secret");
  REQUIRE(m.outcome == MatchOutcome::Found);
  REQUIRE(m.params.size() == 1);
  CHECK(m.params[0].second == "secret");
}
