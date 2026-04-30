#include "atria/method.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/router.hpp"
#include "atria/status.hpp"

#include "timer.hpp"

#include <cstdio>
#include <string>

int main() {
  atria::Router router;

  // A representative route table with literals and parameters.
  (void)router.add(atria::Method::Get, "/health", [](atria::Request&) {
    return atria::Response{atria::Status::Ok};
  });
  (void)router.add(atria::Method::Get, "/api/v1/items", [](atria::Request&) {
    return atria::Response{atria::Status::Ok};
  });
  (void)router.add(atria::Method::Get, "/api/v1/items/{id}", [](atria::Request&) {
    return atria::Response{atria::Status::Ok};
  });
  (void)router.add(atria::Method::Get, "/api/v1/users/{id}/posts/{post_id}",
                   [](atria::Request&) { return atria::Response{atria::Status::Ok}; });
  (void)router.add(atria::Method::Post, "/api/v1/items", [](atria::Request&) {
    return atria::Response{atria::Status::Ok};
  });

  std::printf("== atria router microbenchmark ==\n");

  atria_bench::run("static literal match", 200000, [&] {
    auto m = router.match(atria::Method::Get, "/api/v1/items");
    if (m.outcome == atria::MatchOutcome::NotFound) {
      std::printf("unexpected miss\n");
    }
  });

  atria_bench::run("single parameter match", 200000, [&] {
    auto m = router.match(atria::Method::Get, "/api/v1/items/12345");
    if (m.outcome == atria::MatchOutcome::NotFound) {
      std::printf("unexpected miss\n");
    }
  });

  atria_bench::run("two-parameter nested match", 200000, [&] {
    auto m = router.match(atria::Method::Get, "/api/v1/users/abc/posts/xyz");
    if (m.outcome == atria::MatchOutcome::NotFound) {
      std::printf("unexpected miss\n");
    }
  });

  atria_bench::run("not-found", 200000, [&] {
    auto m = router.match(atria::Method::Get, "/no/such/path");
    if (m.outcome != atria::MatchOutcome::NotFound) {
      std::printf("unexpected hit\n");
    }
  });

  return 0;
}
