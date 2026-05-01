#include "atria/method.hpp"

#include <string_view>

namespace atria {

std::string_view to_string(Method method) noexcept {
  switch (method) {
  case Method::Get:
    return "GET";
  case Method::Post:
    return "POST";
  case Method::Put:
    return "PUT";
  case Method::Patch:
    return "PATCH";
  case Method::Delete:
    return "DELETE";
  case Method::Options:
    return "OPTIONS";
  case Method::Head:
    return "HEAD";
  case Method::Subscribe:
    return "SUBSCRIBE";
  case Method::Unsubscribe:
    return "UNSUBSCRIBE";
  case Method::Notify:
    return "NOTIFY";
  }
  return "GET";
}

bool parse_method(std::string_view text, Method& out) noexcept {
  if (text == "GET") {
    out = Method::Get;
    return true;
  }
  if (text == "POST") {
    out = Method::Post;
    return true;
  }
  if (text == "PUT") {
    out = Method::Put;
    return true;
  }
  if (text == "PATCH") {
    out = Method::Patch;
    return true;
  }
  if (text == "DELETE") {
    out = Method::Delete;
    return true;
  }
  if (text == "OPTIONS") {
    out = Method::Options;
    return true;
  }
  if (text == "HEAD") {
    out = Method::Head;
    return true;
  }
  if (text == "SUBSCRIBE") {
    out = Method::Subscribe;
    return true;
  }
  if (text == "UNSUBSCRIBE") {
    out = Method::Unsubscribe;
    return true;
  }
  if (text == "NOTIFY") {
    out = Method::Notify;
    return true;
  }
  return false;
}

}  // namespace atria
