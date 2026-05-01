#include "atria/url.hpp"

#include <catch2/catch_test_macros.hpp>

using atria::normalize_path;
using atria::parse_query;
using atria::percent_decode;

TEST_CASE("percent decoding handles plus and escapes", "[url]") {
  auto decoded = percent_decode("hello%20world+again");
  REQUIRE(decoded.has_value());
  CHECK(*decoded == "hello world again");
}

TEST_CASE("percent decoding can preserve plus for path segments", "[url]") {
  auto decoded = percent_decode("one+two%20three", false);
  REQUIRE(decoded.has_value());
  CHECK(*decoded == "one+two three");
}

TEST_CASE("percent decoding rejects invalid escapes", "[url]") {
  CHECK_FALSE(percent_decode("%XX").has_value());
  CHECK_FALSE(percent_decode("%2").has_value());
}

TEST_CASE("path normalization decodes and resolves dot segments", "[url]") {
  auto normalized = normalize_path("/api/%2E%2E/files/hello%20world");
  REQUIRE(normalized.has_value());
  CHECK(*normalized == "/files/hello world");
}

TEST_CASE("path normalization rejects encoded path separators", "[url]") {
  CHECK_FALSE(normalize_path("/files/a%2Fb").has_value());
  CHECK_FALSE(normalize_path("/files/a%5Cb").has_value());
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
