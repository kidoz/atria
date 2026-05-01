#include "atria/range.hpp"

#include <catch2/catch_test_macros.hpp>

using atria::parse_byte_range;
using atria::Status;

TEST_CASE("parses explicit byte ranges", "[range]") {
  auto range = parse_byte_range("bytes=10-19", 100);
  REQUIRE(range.has_value());
  CHECK(range->first == 10);
  CHECK(range->last == 19);
  CHECK(range->size() == 10);
}

TEST_CASE("parses open ended byte ranges", "[range]") {
  auto range = parse_byte_range("bytes=95-", 100);
  REQUIRE(range.has_value());
  CHECK(range->first == 95);
  CHECK(range->last == 99);
}

TEST_CASE("parses suffix byte ranges", "[range]") {
  auto range = parse_byte_range("bytes=-5", 100);
  REQUIRE(range.has_value());
  CHECK(range->first == 95);
  CHECK(range->last == 99);
}

TEST_CASE("rejects unsupported byte ranges", "[range]") {
  CHECK_FALSE(parse_byte_range("items=0-1", 100).has_value());
  CHECK_FALSE(parse_byte_range("bytes=0-1,4-5", 100).has_value());
  auto unsatisfied = parse_byte_range("bytes=100-", 100);
  REQUIRE_FALSE(unsatisfied.has_value());
  CHECK(unsatisfied.error().status == Status::RangeNotSatisfiable);
}
