#include "atria/method.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/router.hpp"
#include "atria/status.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] atria::Handler ok_handler() {
  return [](atria::Request&) { return atria::Response::empty(atria::Status::Ok); };
}

void add_route(atria::Router& router, atria::Method method, std::string_view path) {
  auto registered = router.add(method, path, ok_handler());
  (void)registered.has_value();
}

[[nodiscard]] atria::Router make_router() {
  atria::Router router;
  add_route(router, atria::Method::Get, "/");
  add_route(router, atria::Method::Get, "/health");
  add_route(router, atria::Method::Get, "/files/{name}");
  add_route(router, atria::Method::Post, "/api/v1/users/{id}");
  add_route(router, atria::Method::Get, "/ws/{room}");
  return router;
}

[[nodiscard]] atria::Method method_from_byte(std::uint8_t byte) {
  switch (byte % 7U) {
  case 0:
    return atria::Method::Get;
  case 1:
    return atria::Method::Post;
  case 2:
    return atria::Method::Put;
  case 3:
    return atria::Method::Patch;
  case 4:
    return atria::Method::Delete;
  case 5:
    return atria::Method::Options;
  default:
    return atria::Method::Head;
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  static const atria::Router router = make_router();
  if (size == 0) {
    return 0;
  }
  auto method = method_from_byte(data[0]);
  std::string_view path{reinterpret_cast<const char*>(data + 1), size - 1};
  auto match = router.match(method, path);
  if (match.outcome == atria::MatchOutcome::Found) {
    atria::Request request;
    request.set_path_params(std::move(match.params));
    (void)match.handler(request);
  }
  return 0;
}
