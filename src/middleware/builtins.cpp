#include "atria/middleware.hpp"

#include "atria/method.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/status.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <utility>

namespace atria::middleware {

Middleware request_logger() {
  return [](Request& req, const Next& next) -> Response {
    Response response = next(req);
    std::fprintf(
        stderr, "[atria] %.*s %.*s -> %u\n", static_cast<int>(to_string(req.method()).size()),
        to_string(req.method()).data(), static_cast<int>(req.path().size()), req.path().data(),
        static_cast<unsigned>(static_cast<unsigned short>(response.status())));
    return response;
  };
}

Middleware cors(std::string allow_origin) {
  return [origin = std::move(allow_origin)](Request& req, const Next& next) -> Response {
    if (req.method() == Method::Options) {
      Response r{Status::NoContent};
      r.set_header("Access-Control-Allow-Origin", origin);
      r.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS, HEAD");
      r.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
      return r;
    }
    Response response = next(req);
    response.set_header("Access-Control-Allow-Origin", origin);
    return response;
  };
}

Middleware error_handler() {
  return [](Request& req, const Next& next) -> Response {
    try {
      return next(req);
    } catch (const std::exception& ex) {
      Response r{Status::InternalServerError};
      r.set_header("Content-Type", "application/json; charset=utf-8");
      std::string body = R"({"error":{"code":"internal_error","message":")";
      for (char c : std::string_view{ex.what()}) {
        if (c == '"' || c == '\\') {
          body.push_back('\\');
        }
        if (c == '\n' || c == '\r' || c == '\t') {
          continue;
        }
        body.push_back(c);
      }
      body.append("\"}}");
      r.set_body(std::move(body));
      return r;
    } catch (...) {
      Response r{Status::InternalServerError};
      r.set_header("Content-Type", "application/json; charset=utf-8");
      r.set_body(R"({"error":{"code":"internal_error","message":"unknown error"}})");
      return r;
    }
  };
}

}  // namespace atria::middleware
