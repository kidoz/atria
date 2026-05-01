#include "atria/json.hpp"
#include "atria/validation.hpp"

#include <catch2/catch_test_macros.hpp>

using atria::Json;
using atria::Validator;
using atria::ValidationError;

TEST_CASE("required field is reported when missing", "[validation]") {
  Json j = Json::object({{"other", 1}});
  Validator v;
  v.required("name", j);
  CHECK_FALSE(v.ok());
  REQUIRE(v.errors().size() == 1);
  CHECK(v.errors()[0].field == "name");
}

TEST_CASE("string min/max are enforced", "[validation]") {
  Json j = Json::object({{"name", "ab"}});
  Validator v;
  v.string_min("name", j, 3);
  CHECK_FALSE(v.ok());

  Json k = Json::object({{"name", "abcdef"}});
  Validator w;
  w.string_max("name", k, 3);
  CHECK_FALSE(w.ok());
}

TEST_CASE("type mismatches surface in errors", "[validation]") {
  Json j = Json::object({{"completed", "yes"}});
  Validator v;
  v.boolean_optional("completed", j);
  CHECK_FALSE(v.ok());
}

TEST_CASE("error response has expected shape", "[validation]") {
  Validator v;
  v.required("name", Json::object({}));
  Json err = atria::to_error_json("validation_error", "Request validation failed", v.error());
  REQUIRE(err.is_object());
  REQUIRE(err.find("error") != nullptr);
  const Json* details = err.find("error")->find("details");
  REQUIRE(details != nullptr);
  REQUIRE(details->is_array());
  CHECK(details->as_array().size() == 1);
  CHECK(details->as_array()[0].find("field")->as_string() == "name");
}
