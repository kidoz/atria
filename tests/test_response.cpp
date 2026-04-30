#include "atria/json.hpp"
#include "atria/response.hpp"
#include "atria/status.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using atria::Json;
using atria::Response;
using atria::Status;

TEST_CASE("text responses serialize with content-type and content-length", "[response]") {
  Response r = Response::text("hi");
  std::string s = r.serialize();
  CHECK(s.find("HTTP/1.1 200 OK\r\n") == 0);
  CHECK(s.find("Content-Type: text/plain") != std::string::npos);
  CHECK(s.find("Content-Length: 2") != std::string::npos);
  CHECK(s.find("\r\n\r\nhi") != std::string::npos);
}

TEST_CASE("json responses set application/json", "[response]") {
  Response r = Response::json(Json::object({{"a", 1}}));
  std::string s = r.serialize();
  CHECK(s.find("Content-Type: application/json") != std::string::npos);
  CHECK(s.find(R"({"a":1})") != std::string::npos);
}

TEST_CASE("empty response uses No Content by default", "[response]") {
  Response r = Response::empty();
  std::string s = r.serialize();
  CHECK(s.find("HTTP/1.1 204 No Content\r\n") == 0);
}

TEST_CASE("CRLF in header values is rejected at serialize time", "[response]") {
  Response r{Status::Ok};
  r.set_header("X-Bad", "evil\r\nInjected: 1");
  std::string s = r.serialize();
  CHECK(s.find("X-Bad: evil") == std::string::npos);
  CHECK(s.find("Injected") == std::string::npos);
}
