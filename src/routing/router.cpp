#include "atria/router.hpp"

#include "atria/method.hpp"
#include "atria/middleware.hpp"
#include "atria/request.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

namespace {

[[nodiscard]] std::vector<std::string> split_path(std::string_view path) {
  std::vector<std::string> segments;
  std::size_t i = 0;
  if (!path.empty() && path.front() == '/') {
    i = 1;
  }
  while (i < path.size()) {
    std::size_t slash = path.find('/', i);
    if (slash == std::string_view::npos) {
      slash = path.size();
    }
    segments.emplace_back(path.substr(i, slash - i));
    i = slash + 1;
  }
  return segments;
}

[[nodiscard]] constexpr std::size_t method_index(Method method) noexcept {
  return static_cast<std::size_t>(method);
}

constexpr std::size_t kMethodCount = 7;

struct Segment {
  std::string text;
  bool is_param{false};
};

[[nodiscard]] std::expected<std::vector<Segment>, RouteError> compile_path(std::string_view path) {
  if (path.empty() || path.front() != '/') {
    return std::unexpected(RouteError{"path must begin with /"});
  }
  auto raw = split_path(path);
  std::vector<Segment> out;
  out.reserve(raw.size());
  std::set<std::string> param_names;
  for (auto& s : raw) {
    if (s.empty()) {
      return std::unexpected(RouteError{"empty path segment"});
    }
    if (s.front() == '{') {
      if (s.back() != '}') {
        return std::unexpected(RouteError{"unclosed parameter segment"});
      }
      std::string name = s.substr(1, s.size() - 2);
      if (name.empty()) {
        return std::unexpected(RouteError{"empty parameter name"});
      }
      if (!param_names.insert(name).second) {
        return std::unexpected(RouteError{"duplicate parameter name: " + name});
      }
      out.push_back({std::move(name), true});
    } else {
      if (s.find('{') != std::string::npos || s.find('}') != std::string::npos) {
        return std::unexpected(RouteError{"unsupported parameter syntax"});
      }
      out.push_back({std::move(s), false});
    }
  }
  return out;
}

struct Node {
  std::vector<std::unique_ptr<Node>> literals;
  std::vector<std::string> literal_keys;

  std::unique_ptr<Node> param_child;
  std::string param_name;

  std::array<Handler, kMethodCount> handlers;
  std::array<bool, kMethodCount> has_handler{};
  std::array<RouteMeta, kMethodCount> metas;

  Node() = default;

  Node* descend_literal(const std::string& key) {
    for (std::size_t i = 0; i < literal_keys.size(); ++i) {
      if (literal_keys[i] == key) {
        return literals[i].get();
      }
    }
    literal_keys.push_back(key);
    literals.push_back(std::make_unique<Node>());
    return literals.back().get();
  }

  Node* descend_param(const std::string& name) {
    if (!param_child) {
      param_child = std::make_unique<Node>();
      param_name = name;
    }
    return param_child.get();
  }

  [[nodiscard]] const Node* find_literal(std::string_view key) const noexcept {
    for (std::size_t i = 0; i < literal_keys.size(); ++i) {
      if (literal_keys[i] == key) {
        return literals[i].get();
      }
    }
    return nullptr;
  }
};

}  // namespace

struct Router::Impl {
  Node root;
};

Router::Router() : impl_(std::make_unique<Impl>()) {}
Router::~Router() = default;
Router::Router(Router&&) noexcept = default;
Router& Router::operator=(Router&&) noexcept = default;

std::expected<RouteMeta*, RouteError> Router::add(Method method, std::string_view path,
                                                    Handler handler) {
  auto compiled = compile_path(path);
  if (!compiled.has_value()) {
    return std::unexpected(compiled.error());
  }
  Node* node = &impl_->root;
  for (auto& seg : *compiled) {
    if (seg.is_param) {
      node = node->descend_param(seg.text);
    } else {
      node = node->descend_literal(seg.text);
    }
  }
  std::size_t idx = method_index(method);
  if (node->has_handler[idx]) {
    return std::unexpected(RouteError{"duplicate route registration"});
  }
  node->handlers[idx] = std::move(handler);
  node->metas[idx].method = method;
  node->metas[idx].path = std::string{path};
  node->has_handler[idx] = true;
  return &node->metas[idx];
}

namespace {

void walk_for_metadata(const Node& node, const std::function<void(const RouteMeta&)>& visitor) {
  for (std::size_t method_idx = 0; method_idx < kMethodCount; ++method_idx) {
    if (node.has_handler[method_idx]) {
      visitor(node.metas[method_idx]);
    }
  }
  for (const auto& literal_child : node.literals) {
    walk_for_metadata(*literal_child, visitor);
  }
  if (node.param_child) {
    walk_for_metadata(*node.param_child, visitor);
  }
}

}  // namespace

void Router::for_each_route(const std::function<void(const RouteMeta&)>& visitor) const {
  walk_for_metadata(impl_->root, visitor);
}

MatchResult Router::match(Method method, std::string_view path) const {
  auto segments = split_path(path);
  const Node* node = &impl_->root;
  Request::PathParams params;
  for (const auto& s : segments) {
    if (s.empty() && segments.size() == 1) {
      break;
    }
    const Node* next = node->find_literal(s);
    if (next != nullptr) {
      node = next;
      continue;
    }
    if (node->param_child) {
      params.emplace_back(node->param_name, s);
      node = node->param_child.get();
      continue;
    }
    return {MatchOutcome::NotFound, {}, {}};
  }

  std::size_t idx = method_index(method);
  if (node->has_handler[idx]) {
    return {MatchOutcome::Found, node->handlers[idx], std::move(params)};
  }
  for (std::size_t i = 0; i < kMethodCount; ++i) {
    if (node->has_handler[i]) {
      return {MatchOutcome::MethodNotAllowed, {}, {}};
    }
  }
  return {MatchOutcome::NotFound, {}, {}};
}

RouteGroup::RouteGroup(Router& router, std::string prefix)
    : router_(router), prefix_(std::move(prefix)) {}

RouteGroup& RouteGroup::use(Middleware middleware) {
  middleware_.push_back(std::move(middleware));
  return *this;
}

Handler RouteGroup::wrap_with_group_middleware(Handler handler) const {
  // Compose group middleware right-to-left around the handler so registration order is
  // execution order: first-registered middleware is the outermost.
  Handler chain = std::move(handler);
  for (auto rit = middleware_.rbegin(); rit != middleware_.rend(); ++rit) {
    chain = [middleware = *rit, next_handler = std::move(chain)](Request& request) {
      return middleware(request, next_handler);
    };
  }
  return chain;
}

RouteMeta* RouteGroup::add(Method method, std::string_view path, Handler handler) {
  std::string full = prefix_;
  if (!path.empty() && path.front() != '/') {
    full.push_back('/');
  }
  full.append(path);
  if (full.empty()) {
    full = "/";
  }
  auto registered = router_.add(method, full, wrap_with_group_middleware(std::move(handler)));
  return registered.has_value() ? *registered : nullptr;
}

RouteBuilder RouteGroup::get(std::string_view path, Handler handler) {
  return RouteBuilder{add(Method::Get, path, std::move(handler))};
}
RouteBuilder RouteGroup::post(std::string_view path, Handler handler) {
  return RouteBuilder{add(Method::Post, path, std::move(handler))};
}
RouteBuilder RouteGroup::put(std::string_view path, Handler handler) {
  return RouteBuilder{add(Method::Put, path, std::move(handler))};
}
RouteBuilder RouteGroup::patch(std::string_view path, Handler handler) {
  return RouteBuilder{add(Method::Patch, path, std::move(handler))};
}
RouteBuilder RouteGroup::del(std::string_view path, Handler handler) {
  return RouteBuilder{add(Method::Delete, path, std::move(handler))};
}
RouteBuilder RouteGroup::options(std::string_view path, Handler handler) {
  return RouteBuilder{add(Method::Options, path, std::move(handler))};
}
RouteBuilder RouteGroup::head(std::string_view path, Handler handler) {
  return RouteBuilder{add(Method::Head, path, std::move(handler))};
}

}  // namespace atria
