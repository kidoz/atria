#include "atria/json.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/status.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using atria::Json;
using atria::JsonKeyStyle;
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

TEST_CASE("xml responses set text/xml", "[response][xml]") {
  Response r = Response::xml("<root />");
  std::string s = r.serialize();
  CHECK(s.find("Content-Type: text/xml; charset=utf-8") != std::string::npos);
  CHECK(s.find("\r\n\r\n<root />") != std::string::npos);
}

TEST_CASE("json responses can serialize with a key style", "[response][json][naming]") {
  Response r = Response::json(
      Json::object({
          {"user_id", 42},
          {"display_name", "Ada"},
      }),
      JsonKeyStyle::CamelCase
  );
  std::string s = r.serialize();
  CHECK(s.find("Content-Type: application/json") != std::string::npos);
  CHECK(s.find(R"({"userId":42,"displayName":"Ada"})") != std::string::npos);
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

TEST_CASE("file responses support byte ranges", "[response][file][range]") {
  const auto path = std::filesystem::temp_directory_path() / "atria-file-response-test.txt";
  {
    std::ofstream out{path, std::ios::binary};
    out << "0123456789";
  }

  atria::Headers headers;
  headers.set("Range", "bytes=2-5");
  atria::Request request{atria::Method::Get, "/media.bin", "", std::move(headers), ""};
  Response response = Response::file(request, path, {.content_type = "video/mp4"});

  CHECK(response.status() == Status::PartialContent);
  CHECK(response.is_streaming());
  CHECK(response.content_length().value_or(0) == 4);
  CHECK(response.headers().find("Content-Range").value_or("") == "bytes 2-5/10");
  CHECK(response.headers().find("Accept-Ranges").value_or("") == "bytes");
  CHECK(response.headers().find("Content-Type").value_or("") == "video/mp4");

  std::filesystem::remove(path);
}

TEST_CASE("file responses handle HEAD without a body provider", "[response][file]") {
  const auto path = std::filesystem::temp_directory_path() / "atria-file-response-head-test.txt";
  {
    std::ofstream out{path, std::ios::binary};
    out << "hello";
  }

  atria::Request request{atria::Method::Head, "/media.bin", "", {}, ""};
  Response response = Response::file(request, path, {.content_type = "audio/mpeg"});

  CHECK(response.status() == Status::Ok);
  CHECK_FALSE(response.is_streaming());
  CHECK(response.headers().find("Content-Length").value_or("") == "5");
  CHECK(response.headers().find("Accept-Ranges").value_or("") == "bytes");

  std::filesystem::remove(path);
}
