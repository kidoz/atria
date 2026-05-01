#include "atria/router.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/status.hpp"
#include "atria/method.hpp"
#include <catch2/catch_test_macros.hpp>
TEST_CASE("matches root", "[router]") {
  atria::Router r;
  auto m = r.add(atria::Method::Get, "/", [](atria::Request&) { return atria::Response{atria::Status::Ok}; });
  REQUIRE(m.has_value());
}
