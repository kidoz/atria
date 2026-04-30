#include "atria/url.hpp"

#include <catch2/catch_test_macros.hpp>

using atria::parse_query;
using atria::percent_decode;

TEST_CASE("percent decoding handles plus and escapes", "[url]") {
  auto decoded = percent_decode("hello%20world+again");
  REQUIRE(decoded.has_value());
  CHECK(*decoded == "hello world again");
}

TEST_CASE("percent decoding rejects invalid escapes", "[url]") {
  CHECK_FALSE(percent_decode("%XX").has_value());
  CHECK_FALSE(percent_decode("%2").has_value());
}

TEST_CASE("query parser splits multiple keys", "[url]") {
  auto pairs = parse_query("a=1&b=2&c=hello%20world");
  REQUIRE(pairs.size() == 3);
  CHECK(pairs[0].first == "a");
  CHECK(pairs[0].second == "1");
  CHECK(pairs[2].second == "hello world");
}

TEST_CASE("query parser handles missing values", "[url]") {
  auto pairs = parse_query("flag&k=v");
  REQUIRE(pairs.size() == 2);
  CHECK(pairs[0].first == "flag");
  CHECK(pairs[0].second.empty());
  CHECK(pairs[1].first == "k");
  CHECK(pairs[1].second == "v");
}
