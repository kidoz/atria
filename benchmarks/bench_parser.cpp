#include "atria/parser.hpp"
#include "atria/server_config.hpp"

#include "timer.hpp"

#include <cstdio>
#include <string>

int main() {
  std::printf("== atria HTTP parser microbenchmark ==\n");

  atria::ParseLimits limits;

  const std::string get_health =
      "GET /health HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "User-Agent: bench\r\n"
      "Accept: application/json\r\n"
      "\r\n";

  const std::string post_json_body = R"({"name":"buy milk","completed":false})";
  const std::string post_json =
      "POST /api/v1/items HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      std::to_string(post_json_body.size()) + "\r\n\r\n" + post_json_body;

  const std::string get_with_query =
      "GET /search?q=hello%20world&limit=10&offset=20&sort=desc HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "Accept: application/json\r\n"
      "\r\n";

  atria_bench::run("parse: GET /health (3 headers)", 200000, [&] {
    auto r = atria::parse_request(get_health, limits);
    if (!r.has_value()) {
      std::printf("parse failed\n");
    }
  });

  atria_bench::run("parse: POST /items (json body)", 200000, [&] {
    auto r = atria::parse_request(post_json, limits);
    if (!r.has_value()) {
      std::printf("parse failed\n");
    }
  });

  atria_bench::run("parse: GET /search?... (4 queries)", 200000, [&] {
    auto r = atria::parse_request(get_with_query, limits);
    if (!r.has_value()) {
      std::printf("parse failed\n");
    }
  });

  return 0;
}
