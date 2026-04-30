#include "atria/json.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using atria::Json;

TEST_CASE("parses JSON primitives", "[json]") {
  CHECK(Json::parse("null")->is_null());
  CHECK(Json::parse("true")->as_bool() == true);
  CHECK(Json::parse("false")->as_bool() == false);
  CHECK(Json::parse("42")->as_int() == 42);
  CHECK(Json::parse("-7")->as_int() == -7);
  CHECK(Json::parse("3.5")->as_double() == 3.5);
  CHECK(Json::parse(R"("hello")")->as_string() == "hello");
}

TEST_CASE("parses arrays and objects", "[json]") {
  auto arr = Json::parse(R"([1, 2, 3])");
  REQUIRE(arr.has_value());
  REQUIRE(arr->is_array());
  CHECK(arr->as_array().size() == 3);
  CHECK(arr->as_array()[1].as_int() == 2);

  auto obj = Json::parse(R"({"name":"Ada","age":42,"active":true})");
  REQUIRE(obj.has_value());
  REQUIRE(obj->is_object());
  CHECK(obj->find("name")->as_string() == "Ada");
  CHECK(obj->find("age")->as_int() == 42);
  CHECK(obj->find("active")->as_bool() == true);
}

TEST_CASE("handles escaped strings", "[json]") {
  auto j = Json::parse(R"("a\nb\t\"c\"")");
  REQUIRE(j.has_value());
  CHECK(j->as_string() == std::string("a\nb\t\"c\""));
}

TEST_CASE("rejects invalid JSON", "[json]") {
  CHECK_FALSE(Json::parse("{").has_value());
  CHECK_FALSE(Json::parse("[1,").has_value());
  CHECK_FALSE(Json::parse(R"({"k": })").has_value());
  CHECK_FALSE(Json::parse(R"("unterminated)").has_value());
  CHECK_FALSE(Json::parse("null garbage").has_value());
}

TEST_CASE("rejects excessive nesting", "[json]") {
  std::string s;
  for (int i = 0; i < 200; ++i) {
    s.push_back('[');
  }
  for (int i = 0; i < 200; ++i) {
    s.push_back(']');
  }
  Json::ParseLimits limits;
  limits.max_depth = 32;
  CHECK_FALSE(Json::parse(s, limits).has_value());
}

TEST_CASE("stringifies values", "[json]") {
  Json j = Json::object({
      {"id", std::int64_t{42}},
      {"name", std::string{"Ada"}},
      {"active", true},
      {"tags", Json::array({Json{"a"}, Json{"b"}})},
  });
  std::string s = j.stringify();
  CHECK(s.find(R"("id":42)") != std::string::npos);
  CHECK(s.find(R"("name":"Ada")") != std::string::npos);
  CHECK(s.find(R"("active":true)") != std::string::npos);
  CHECK(s.find(R"("tags":["a","b"])") != std::string::npos);
}

TEST_CASE("round-trips parse and stringify", "[json]") {
  std::string original = R"({"a":1,"b":[true,null,"x"]})";
  auto parsed = Json::parse(original);
  REQUIRE(parsed.has_value());
  CHECK(parsed->stringify() == original);
}
