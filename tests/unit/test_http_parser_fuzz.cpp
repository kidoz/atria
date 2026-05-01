// Fuzz-like parser tests: a table of malformed HTTP/1.1 inputs that must each be rejected
// without crashing or hanging. Inputs cover bad request lines, bad headers, body-related
// header conflicts, oversized payloads, and CR/LF / null injection.

#include "atria/parser.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using atria::ParseLimits;
using atria::Status;
using atria::parse_request;

namespace {

struct RejectCase {
  std::string_view label;
  std::string raw;
};

[[nodiscard]] std::vector<RejectCase> base_cases() {
  std::vector<RejectCase> cases;

  cases.push_back({"empty input", ""});
  cases.push_back({"only CRLF", "\r\n\r\n"});
  cases.push_back({"missing version", "GET /\r\n\r\n"});
  cases.push_back({"missing path", "GET  HTTP/1.1\r\n\r\n"});
  cases.push_back({"path without leading slash", "GET foo HTTP/1.1\r\n\r\n"});
  cases.push_back({"unknown HTTP version", "GET / HTTP/9.9\r\n\r\n"});
  cases.push_back({"unsupported method", "BREW / HTTP/1.1\r\n\r\n"});
  cases.push_back({"invalid header name with space", "GET / HTTP/1.1\r\nBad Header: x\r\n\r\n"});
  cases.push_back(
      {"invalid header name with control char",
       std::string("GET / HTTP/1.1\r\nX\x01:y\r\n\r\n")});
  cases.push_back({"header without colon", "GET / HTTP/1.1\r\nNoColonHere\r\n\r\n"});
  cases.push_back({"header with CR in value (not part of CRLF)",
                   std::string("GET / HTTP/1.1\r\nX-Bad: a\rb\r\n\r\n")});
  {
    // Embedded NUL via explicit size — count: 16 (request line+CRLF) + 7 (header name+":")
    // + 1 (space) + 1 (a) + 1 (NUL) + 1 (b) + 2 (CRLF) + 2 (final CRLF) = 31.
    constexpr char kNullHeader[] = "GET / HTTP/1.1\r\nX-Null: a\x00""b\r\n\r\n";
    cases.push_back({"header value with NUL byte",
                     std::string(kNullHeader, sizeof(kNullHeader) - 1)});
  }
  cases.push_back({"unsupported transfer-encoding (e.g. gzip) is rejected",
                   "POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n"});
  cases.push_back({"chunked with no chunks and no terminator is incomplete",
                   "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"});
  cases.push_back({"chunked with malformed size",
                   "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nabc\r\n0\r\n\r\n"});
  cases.push_back({"chunked with conflicting Content-Length",
                   "POST / HTTP/1.1\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nabc"});
  cases.push_back({"content-length non-numeric", "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n"});
  cases.push_back({"content-length empty", "POST / HTTP/1.1\r\nContent-Length: \r\n\r\n"});
  cases.push_back({"content-length signed minus",
                   "POST / HTTP/1.1\r\nContent-Length: -5\r\n\r\n"});
  cases.push_back({"truncated body", "POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nshort"});
  cases.push_back({"missing terminator", "GET / HTTP/1.1\r\nHost: x\r\n"});
  cases.push_back({"body without content-length on POST is allowed but consumed as zero — "
                   "rejected here only if length is mismatched",
                   "POST / HTTP/1.1\r\nContent-Length: 1\r\n\r\n"});

  return cases;
}

}  // namespace

TEST_CASE("malformed HTTP requests are rejected without crashing", "[parser][fuzz]") {
  ParseLimits limits;
  for (const auto& c : base_cases()) {
    INFO("case: " << c.label);
    auto result = parse_request(c.raw, limits);
    REQUIRE_FALSE(result.has_value());
  }
}

TEST_CASE("oversized inputs are rejected with PayloadTooLarge", "[parser][fuzz]") {
  ParseLimits tight;
  tight.max_request_line_bytes = 64;
  tight.max_header_bytes = 256;
  tight.max_header_count = 4;
  tight.max_body_bytes = 16;

  SECTION("request line too long") {
    std::string raw = "GET /";
    raw.append(200, 'a');
    raw.append(" HTTP/1.1\r\nHost: x\r\n\r\n");
    auto r = parse_request(raw, tight);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().status == Status::PayloadTooLarge);
  }

  SECTION("too many headers") {
    std::string raw = "GET / HTTP/1.1\r\n";
    for (int i = 0; i < 50; ++i) {
      raw.append("X-").append(std::to_string(i)).append(": v\r\n");
    }
    raw.append("\r\n");
    auto r = parse_request(raw, tight);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().status == Status::PayloadTooLarge);
  }

  SECTION("header section too large") {
    std::string raw = "GET / HTTP/1.1\r\n";
    raw.append("X-Pad: ").append(1024, 'p').append("\r\n\r\n");
    auto r = parse_request(raw, tight);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().status == Status::PayloadTooLarge);
  }

  SECTION("body too large") {
    std::string raw =
        "POST / HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 1024\r\n"
        "\r\n";
    raw.append(1024, 'a');
    auto r = parse_request(raw, tight);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().status == Status::PayloadTooLarge);
  }
}

TEST_CASE("parser handles random byte salads without crashing", "[parser][fuzz]") {
  ParseLimits limits;
  // Crafted bytes that exercise edge cases: high-bit bytes, unbalanced CRs, lone LFs,
  // tabs in values, etc. None of these should crash; all should produce an error or a
  // Request with bounded contents.
  const std::vector<std::string> samples = {
      std::string{"\x00\x01\x02\x03\x04", 5},
      std::string{"\xff\xfe\xfd\xfc\xfb", 5},
      std::string{"\r\r\r\r\r\r\r\r"},
      std::string{"\n\n\n\n\n\n"},
      std::string{"GET / HTTP/1.1\nHost: x\n\n"},  // bare LF, not CRLF
      std::string{"\x80GET / HTTP/1.1\r\nHost: x\r\n\r\n"},
      std::string{"GET / HTTP/1.1\r\nX-T: \tvalue with tab\t\r\n\r\n"},  // tab is allowed
  };

  for (const auto& s : samples) {
    INFO("sample size: " << s.size());
    // We don't assert success/failure — only that we don't crash and
    // we don't hang. parse_request is purely synchronous and bounded.
    auto _ = parse_request(s, limits);
    (void)_;
  }
}
