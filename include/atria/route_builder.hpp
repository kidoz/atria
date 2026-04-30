#pragma once

#include "atria/json.hpp"
#include "atria/openapi.hpp"
#include "atria/route_meta.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atria {

// Fluent builder returned by Application::get / RouteGroup::get / etc. so users can
// attach OpenAPI metadata to a just-registered route without breaking the existing
// statement-style registration.
//
// Example:
//   app.post("/items", create_item)
//      .name("createItem")
//      .summary("Create an item")
//      .tag("items")
//      .request_body<CreateItemDto>()
//      .response<ItemDto>(201, "Item created");
//
// The builder writes back to a stable RouteMeta slot owned by the Router or by a
// WebSocket route record. If a default-constructed RouteBuilder is destroyed without
// modifications, it is a no-op (the slot is already populated with the route kind/path).
class RouteBuilder {
public:
  RouteBuilder() = default;

  explicit RouteBuilder(RouteMeta* slot) noexcept : slot_(slot) {}

  RouteBuilder& name(std::string operation_id) {
    if (slot_ != nullptr) {
      slot_->operation_id = std::move(operation_id);
    }
    return *this;
  }

  RouteBuilder& summary(std::string text) {
    if (slot_ != nullptr) {
      slot_->summary = std::move(text);
    }
    return *this;
  }

  RouteBuilder& description(std::string text) {
    if (slot_ != nullptr) {
      slot_->description = std::move(text);
    }
    return *this;
  }

  RouteBuilder& tag(std::string name) {
    if (slot_ != nullptr) {
      slot_->tags.push_back(std::move(name));
    }
    return *this;
  }

  RouteBuilder& deprecated(bool value = true) {
    if (slot_ != nullptr) {
      slot_->deprecated = value;
    }
    return *this;
  }

  // Attach an explicit request-body schema (use when there's no static type to refer to).
  RouteBuilder&
  request_body(Json schema, std::string content_type = "application/json", bool required = true) {
    if (slot_ != nullptr) {
      slot_->request_body = RouteRequestBody{
          .content_type = std::move(content_type),
          .schema = std::move(schema),
          .required = required,
      };
    }
    return *this;
  }

  // Type-driven request body — uses openapi::schema_for<T>::get() to derive the schema.
  template <typename T>
  RouteBuilder& request_body(std::string content_type = "application/json", bool required = true) {
    return request_body(openapi::schema_for<T>::get(), std::move(content_type), required);
  }

  // Attach an explicit response schema for a given status code.
  RouteBuilder& response(
      int status_code,
      Json schema,
      std::string description = {},
      std::string content_type = "application/json"
  ) {
    if (slot_ != nullptr) {
      slot_->responses.push_back(
          RouteResponseSpec{
              .status_code = status_code,
              .description = std::move(description),
              .schema = std::move(schema),
              .content_type = std::move(content_type),
          }
      );
    }
    return *this;
  }

  // Type-driven response — schema derived from openapi::schema_for<T>.
  template <typename T>
  RouteBuilder& response(
      int status_code,
      std::string description = {},
      std::string content_type = "application/json"
  ) {
    return response(
        status_code,
        openapi::schema_for<T>::get(),
        std::move(description),
        std::move(content_type)
    );
  }

  // Response with no body (e.g. 204, 404).
  RouteBuilder& response_empty(int status_code, std::string description = {}) {
    if (slot_ != nullptr) {
      slot_->responses.push_back(
          RouteResponseSpec{
              .status_code = status_code,
              .description = std::move(description),
              .schema = std::nullopt,
              .content_type = std::string{},
          }
      );
    }
    return *this;
  }

private:
  RouteMeta* slot_{nullptr};
};

}  // namespace atria
