#pragma once

#include "atria/json.hpp"
#include "atria/method.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace atria {

enum class RouteKind : std::uint8_t {
  Http,
  WebSocket,
};

struct RouteRequestBody {
  std::string content_type{"application/json"};
  Json schema;
  bool required{true};
};

struct RouteResponseSpec {
  int status_code{0};
  std::string description;
  std::optional<Json> schema;
  std::string content_type{"application/json"};
};

// Metadata attached to a single (method, path) route. Filled via the fluent RouteBuilder
// API and consumed by the OpenAPI emitter.
struct RouteMeta {
  RouteKind kind{RouteKind::Http};
  Method method{Method::Get};
  std::string path;  // canonical, with `{name}` placeholders preserved
  std::string operation_id;
  std::string summary;
  std::string description;
  std::vector<std::string> tags;
  std::optional<RouteRequestBody> request_body;
  std::vector<RouteResponseSpec> responses;
  bool deprecated{false};
};

}  // namespace atria
