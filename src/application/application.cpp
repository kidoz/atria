#include "atria/application.hpp"

#include "atria/json.hpp"
#include "atria/method.hpp"
#include "atria/middleware.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/route_meta.hpp"
#include "atria/router.hpp"
#include "atria/server_config.hpp"
#include "atria/status.hpp"
#include "atria/url.hpp"
#include "atria/websocket.hpp"
#include "net/server_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

namespace {

[[nodiscard]] Response not_found_response() {
  Response r{Status::NotFound};
  r.set_header("Content-Type", "application/json; charset=utf-8");
  r.set_body(R"({"error":{"code":"not_found","message":"Resource not found"}})");
  return r;
}

[[nodiscard]] Response method_not_allowed_response() {
  Response r{Status::MethodNotAllowed};
  r.set_header("Content-Type", "application/json; charset=utf-8");
  r.set_body(R"({"error":{"code":"method_not_allowed","message":"Method not allowed"}})");
  return r;
}

}  // namespace

Application::Application() = default;
Application::~Application() = default;

Application& Application::use(Middleware middleware) {
  middleware_.push_back(std::move(middleware));
  return *this;
}

namespace {

[[nodiscard]] RouteBuilder make_builder(std::expected<RouteMeta*, RouteError> registered) {
  return RouteBuilder{registered.has_value() ? *registered : nullptr};
}

}  // namespace

RouteBuilder Application::get(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Get, path, std::move(handler)));
}

RouteBuilder Application::post(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Post, path, std::move(handler)));
}

RouteBuilder Application::put(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Put, path, std::move(handler)));
}

RouteBuilder Application::patch(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Patch, path, std::move(handler)));
}

RouteBuilder Application::del(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Delete, path, std::move(handler)));
}

RouteBuilder Application::options(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Options, path, std::move(handler)));
}

RouteBuilder Application::head(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Head, path, std::move(handler)));
}

RouteBuilder Application::subscribe(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Subscribe, path, std::move(handler)));
}

RouteBuilder Application::unsubscribe(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Unsubscribe, path, std::move(handler)));
}

RouteBuilder Application::notify(std::string_view path, Handler handler) {
  return make_builder(router_.add(Method::Notify, path, std::move(handler)));
}

RouteBuilder Application::websocket(std::string_view path, WebSocketHandler handler) {
  auto route = std::make_unique<WebSocketRoute>();
  route->path = std::string{path};
  route->handler = std::move(handler);
  route->meta.kind = RouteKind::WebSocket;
  route->meta.method = Method::Get;
  route->meta.path = route->path;
  RouteMeta* meta = &route->meta;
  websocket_routes_.push_back(std::move(route));
  return RouteBuilder{meta};
}

Application&
Application::group(std::string_view prefix, const std::function<void(RouteGroup&)>& builder) {
  std::string normalized{prefix};
  if (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  RouteGroup g{router_, std::move(normalized)};
  builder(g);
  return *this;
}

Response Application::dispatch(Request& request) {
  auto match = router_.match(request.method(), request.path());

  Handler terminal;
  if (match.outcome == MatchOutcome::Found) {
    request.set_path_params(std::move(match.params));
    terminal = match.handler;
  } else if (match.outcome == MatchOutcome::MethodNotAllowed) {
    terminal = [](Request&) { return method_not_allowed_response(); };
  } else {
    terminal = [](Request&) { return not_found_response(); };
  }

  Next chain = [terminal](Request& req) { return terminal(req); };
  for (auto it = middleware_.rbegin(); it != middleware_.rend(); ++it) {
    chain = [mw = *it, next = std::move(chain)](Request& req) { return mw(req, next); };
  }
  return chain(request);
}

namespace {

[[nodiscard]] std::vector<std::string_view> split_view_path(std::string_view path) {
  std::vector<std::string_view> segments;
  std::size_t cursor = 0;
  if (!path.empty() && path.front() == '/') {
    cursor = 1;
  }
  while (cursor < path.size()) {
    auto slash = path.find('/', cursor);
    if (slash == std::string_view::npos) {
      slash = path.size();
    }
    segments.push_back(path.substr(cursor, slash - cursor));
    cursor = slash + 1;
  }
  return segments;
}

[[nodiscard]] bool is_param_segment(std::string_view segment) noexcept {
  return segment.size() > 2 && segment.front() == '{' && segment.back() == '}';
}

[[nodiscard]] std::optional<Request::PathParams>
match_websocket_path(std::string_view pattern, std::string_view path) {
  auto pattern_segments = split_view_path(pattern);
  auto path_segments = decode_path_segments(path);
  if (!path_segments.has_value() || pattern_segments.size() != path_segments->size()) {
    return std::nullopt;
  }

  Request::PathParams params;
  for (std::size_t i = 0; i < pattern_segments.size(); ++i) {
    auto pattern_segment = pattern_segments.at(i);
    const auto& path_segment = path_segments->at(i);
    if (is_param_segment(pattern_segment)) {
      params.emplace_back(
          std::string{pattern_segment.substr(1, pattern_segment.size() - 2)},
          path_segment
      );
      continue;
    }
    if (pattern_segment != path_segment) {
      return std::nullopt;
    }
  }
  return params;
}

}  // namespace

bool Application::dispatch_websocket(Request& request, WebSocketSession& session) {
  for (const auto& route : websocket_routes_) {
    auto params = match_websocket_path(route->path, request.path());
    if (!params.has_value()) {
      continue;
    }
    request.set_path_params(std::move(*params));
    session.set_request(&request);
    route->handler(session);
    return true;
  }
  return false;
}

int Application::listen(ServerConfig config) {
  net::ServerRuntime runtime{*this, config};
  return runtime.run(running_);
}

void Application::shutdown() noexcept {
  running_.store(false);
}

Application& Application::info(std::string title, std::string version) {
  openapi_title_ = std::move(title);
  openapi_version_ = std::move(version);
  return *this;
}

namespace {

[[nodiscard]] std::vector<std::string> extract_path_param_names(std::string_view path) {
  std::vector<std::string> names;
  std::size_t cursor = 0;
  while (cursor < path.size()) {
    auto open = path.find('{', cursor);
    if (open == std::string_view::npos) {
      break;
    }
    auto close = path.find('}', open + 1);
    if (close == std::string_view::npos) {
      break;
    }
    names.emplace_back(path.substr(open + 1, close - open - 1));
    cursor = close + 1;
  }
  return names;
}

[[nodiscard]] std::string method_to_lower(Method method) {
  std::string text{to_string(method)};
  for (auto& character : text) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character + ('a' - 'A'));
    }
  }
  return text;
}

[[nodiscard]] std::string_view
websocket_message_direction_name(WebSocketMessageDirection direction) noexcept {
  switch (direction) {
  case WebSocketMessageDirection::Receive:
    return "receive";
  case WebSocketMessageDirection::Send:
    return "send";
  }
  return "receive";
}

[[nodiscard]] Json build_websocket_message_object(const RouteWebSocketMessageSpec& message) {
  Json::Object entry;
  entry.emplace_back("direction", std::string{websocket_message_direction_name(message.direction)});
  if (!message.description.empty()) {
    entry.emplace_back("description", message.description);
  }
  Json::Object content;
  content.emplace_back(message.content_type, Json::object({{"schema", message.schema}}));
  entry.emplace_back("content", Json{std::move(content)});
  return Json{std::move(entry)};
}

[[nodiscard]] Json build_operation_object(const RouteMeta& meta) {
  Json::Object operation;
  if (meta.kind == RouteKind::WebSocket) {
    operation.emplace_back("x-atria-websocket", true);
  }
  if (!meta.operation_id.empty()) {
    operation.emplace_back("operationId", meta.operation_id);
  }
  if (!meta.summary.empty()) {
    operation.emplace_back("summary", meta.summary);
  }
  if (!meta.description.empty()) {
    operation.emplace_back("description", meta.description);
  }
  if (!meta.tags.empty()) {
    Json::Array tags;
    tags.reserve(meta.tags.size());
    for (const auto& tag : meta.tags) {
      tags.emplace_back(tag);
    }
    operation.emplace_back("tags", Json{std::move(tags)});
  }
  if (meta.deprecated) {
    operation.emplace_back("deprecated", true);
  }
  if (meta.kind == RouteKind::WebSocket && !meta.websocket_subprotocols.empty()) {
    Json::Array subprotocols;
    subprotocols.reserve(meta.websocket_subprotocols.size());
    for (const auto& subprotocol : meta.websocket_subprotocols) {
      subprotocols.emplace_back(subprotocol);
    }
    operation.emplace_back("x-atria-websocket-subprotocols", Json{std::move(subprotocols)});
  }
  if (meta.kind == RouteKind::WebSocket && !meta.websocket_messages.empty()) {
    Json::Array messages;
    messages.reserve(meta.websocket_messages.size());
    for (const auto& message : meta.websocket_messages) {
      messages.push_back(build_websocket_message_object(message));
    }
    operation.emplace_back("x-atria-websocket-messages", Json{std::move(messages)});
  }

  // Path parameters extracted from the URL template.
  Json::Array parameters;
  for (const auto& name : extract_path_param_names(meta.path)) {
    parameters.push_back(
        Json::object({
            {"name", name},
            {"in", std::string{"path"}},
            {"required", true},
            {"schema", Json::object({{"type", std::string{"string"}}})},
        })
    );
  }
  if (!parameters.empty()) {
    operation.emplace_back("parameters", Json{std::move(parameters)});
  }

  if (meta.request_body.has_value()) {
    Json::Object content;
    content.emplace_back(
        meta.request_body->content_type,
        Json::object({{"schema", meta.request_body->schema}})
    );
    operation.emplace_back(
        "requestBody",
        Json::object(
            {{"required", meta.request_body->required}, {"content", Json{std::move(content)}}}
        )
    );
  }

  Json::Object responses;
  if (meta.responses.empty()) {
    if (meta.kind == RouteKind::WebSocket) {
      responses.emplace_back(
          "101",
          Json::object({{"description", std::string{"Switching Protocols"}}})
      );
    } else {
      responses.emplace_back("default", Json::object({{"description", std::string{"OK"}}}));
    }
  } else {
    for (const auto& response : meta.responses) {
      Json::Object response_obj;
      response_obj.emplace_back(
          "description",
          response.description.empty() ? std::string{""} : response.description
      );
      if (response.schema.has_value() && !response.content_type.empty()) {
        Json::Object content;
        content.emplace_back(response.content_type, Json::object({{"schema", *response.schema}}));
        response_obj.emplace_back("content", Json{std::move(content)});
      }
      responses.emplace_back(std::to_string(response.status_code), Json{std::move(response_obj)});
    }
  }
  operation.emplace_back("responses", Json{std::move(responses)});
  return Json{std::move(operation)};
}

void add_path_operation(
    std::vector<std::pair<std::string, Json::Object>>& path_entries,
    const RouteMeta& meta,
    std::string operation_name
) {
  auto entry = std::ranges::find_if(path_entries, [&](const auto& candidate) {
    return candidate.first == meta.path;
  });
  if (entry == path_entries.end()) {
    path_entries.emplace_back(meta.path, Json::Object{});
    entry = std::prev(path_entries.end());
  }
  entry->second.emplace_back(std::move(operation_name), build_operation_object(meta));
}

}  // namespace

Json Application::openapi_json() const {
  Json::Object root;
  root.emplace_back("openapi", std::string{"3.0.3"});
  root.emplace_back(
      "info",
      Json::object({
          {"title", openapi_title_},
          {"version", openapi_version_},
      })
  );

  // Group operations by path. Each path has an object whose keys are lower-case method
  // names. We collect into a vector first so traversal order is stable.
  std::vector<std::pair<std::string, Json::Object>> path_entries;
  router_.for_each_route([&](const RouteMeta& meta) {
    add_path_operation(path_entries, meta, method_to_lower(meta.method));
  });
  for (const auto& route : websocket_routes_) {
    add_path_operation(path_entries, route->meta, "x-atria-websocket");
  }

  Json::Object paths;
  for (auto& [path, operations] : path_entries) {
    paths.emplace_back(path, Json{std::move(operations)});
  }
  root.emplace_back("paths", Json{std::move(paths)});
  return Json{std::move(root)};
}

}  // namespace atria
