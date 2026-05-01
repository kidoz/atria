#include "atria/parser.hpp"
#include "atria/server_config.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using atria::Method;
using atria::parse_request;
using atria::ParseLimits;
using atria::Status;

namespace {

constexpr ParseLimits kDefaultLimits{};

}  // namespace

TEST_CASE("parses a simple GET request", "[parser]") {
  std::string raw = "GET /health HTTP/1.1\r\n"
                    "Host: example.com\r\n"
                    "User-Agent: catch2\r\n"
                    "\r\n";

  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(parsed.has_value());
  CHECK(parsed->method() == Method::Get);
  CHECK(parsed->path() == "/health");
  CHECK(parsed->header("host").value_or("") == "example.com");
  CHECK(parsed->body().empty());
}

TEST_CASE("parses POST with content-length body", "[parser]") {
  std::string body = R"({"name":"a"})";
  std::string raw = "POST /api/v1/items HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " +
                    std::to_string(body.size()) +
                    "\r\n"
                    "\r\n" +
                    body;
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(parsed.has_value());
  CHECK(parsed->method() == Method::Post);
  CHECK(parsed->path() == "/api/v1/items");
  CHECK(parsed->body() == body);
}

TEST_CASE("parses query parameters", "[parser]") {
  std::string raw = "GET /search?q=hello%20world&limit=10 HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(parsed.has_value());
  CHECK(parsed->path() == "/search");
  CHECK(parsed->query("q").value_or("") == "hello world");
  CHECK(parsed->query("limit").value_or("") == "10");
}

TEST_CASE("normalizes decoded request path before dispatch", "[parser]") {
  std::string raw = "GET /api/%2E%2E/users/John%20Doe HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(parsed.has_value());
  CHECK(parsed->path() == "/users/John Doe");
}

TEST_CASE("rejects invalid escaped request path", "[parser]") {
  std::string invalid_escape = "GET /files/%XX HTTP/1.1\r\n"
                               "Host: x\r\n"
                               "\r\n";
  auto parsed_invalid_escape = parse_request(invalid_escape, kDefaultLimits);
  REQUIRE(!parsed_invalid_escape.has_value());
  CHECK(parsed_invalid_escape.error().status == Status::BadRequest);

  std::string encoded_separator = "GET /files/a%2Fb HTTP/1.1\r\n"
                                  "Host: x\r\n"
                                  "\r\n";
  auto parsed_encoded_separator = parse_request(encoded_separator, kDefaultLimits);
  REQUIRE(!parsed_encoded_separator.has_value());
  CHECK(parsed_encoded_separator.error().status == Status::BadRequest);
}

TEST_CASE("rejects malformed request line", "[parser]") {
  std::string raw = "GET\r\nHost: x\r\n\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(!parsed.has_value());
  CHECK(parsed.error().status == Status::BadRequest);
}

TEST_CASE("rejects unsupported HTTP version", "[parser]") {
  std::string raw = "GET / HTTP/2.0\r\n"
                    "Host: x\r\n"
                    "\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(!parsed.has_value());
}

TEST_CASE("rejects unsupported method", "[parser]") {
  std::string raw = "BREW / HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(!parsed.has_value());
  CHECK(parsed.error().status == Status::NotImplemented);
}

TEST_CASE("rejects oversized header section", "[parser]") {
  std::string raw = "GET / HTTP/1.1\r\n"
                    "Host: x\r\n";
  for (int i = 0; i < 200; ++i) {
    raw.append("X-Pad-")
        .append(std::to_string(i))
        .append(": ")
        .append(std::string(80, 'a'))
        .append("\r\n");
  }
  raw.append("\r\n");
  ParseLimits limits{};
  limits.max_header_bytes = 1024;
  auto parsed = parse_request(raw, limits);
  REQUIRE(!parsed.has_value());
  CHECK(parsed.error().status == Status::PayloadTooLarge);
}

TEST_CASE("rejects oversized body", "[parser]") {
  std::string raw = "POST / HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Content-Length: 100000\r\n"
                    "\r\n";
  raw.append(std::string(100000, 'a'));
  ParseLimits limits{};
  limits.max_body_bytes = 1024;
  auto parsed = parse_request(raw, limits);
  REQUIRE(!parsed.has_value());
  CHECK(parsed.error().status == Status::PayloadTooLarge);
}

TEST_CASE("rejects invalid Content-Length", "[parser]") {
  std::string raw = "POST / HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Content-Length: abc\r\n"
                    "\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(!parsed.has_value());
}

TEST_CASE("decodes Transfer-Encoding: chunked body", "[parser]") {
  std::string raw = "POST /upload HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "5\r\nhello\r\n"
                    "1\r\n,\r\n"
                    "6\r\nworld!\r\n"
                    "0\r\n\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(parsed.has_value());
  CHECK(parsed->method() == Method::Post);
  CHECK(parsed->path() == "/upload");
  CHECK(parsed->body() == "hello,world!");
}

TEST_CASE("rejects unsupported Transfer-Encoding values", "[parser]") {
  std::string raw = "POST / HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Transfer-Encoding: gzip\r\n"
                    "\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(!parsed.has_value());
  CHECK(parsed.error().status == Status::NotImplemented);
}

TEST_CASE("rejects Content-Length together with Transfer-Encoding", "[parser]") {
  std::string raw = "POST / HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Content-Length: 3\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "abc";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(!parsed.has_value());
  CHECK(parsed.error().status == Status::BadRequest);
}

TEST_CASE("rejects invalid header name", "[parser]") {
  std::string raw = "GET / HTTP/1.1\r\n"
                    "Bad Header: x\r\n"
                    "\r\n";
  auto parsed = parse_request(raw, kDefaultLimits);
  REQUIRE(!parsed.has_value());
}
