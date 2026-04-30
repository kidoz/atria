#include "atria/json.hpp"

#include "timer.hpp"

#include <cstdio>
#include <string>

int main() {
  std::printf("== atria JSON microbenchmark ==\n");

  const std::string small = R"({"id":42,"name":"Ada","active":true})";
  const std::string nested =
      R"({"user":{"id":42,"name":"Ada","tags":["a","b","c"]},"meta":{"v":1.5,"flags":[true,false,null]}})";

  atria_bench::run("parse: small object", 100000, [&] {
    auto r = atria::Json::parse(small);
    if (!r.has_value()) {
      std::printf("parse failed\n");
    }
  });

  atria_bench::run("parse: nested mixed", 50000, [&] {
    auto r = atria::Json::parse(nested);
    if (!r.has_value()) {
      std::printf("parse failed\n");
    }
  });

  auto value = atria::Json::object({
      {"id", 42},
      {"name", std::string{"Ada"}},
      {"active", true},
      {"tags", atria::Json::array({atria::Json{"a"}, atria::Json{"b"}, atria::Json{"c"}})},
  });

  atria_bench::run("stringify: small object", 100000, [&] {
    auto s = value.stringify();
    if (s.empty()) {
      std::printf("stringify failed\n");
    }
  });

  return 0;
}
