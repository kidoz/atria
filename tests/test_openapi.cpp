// Tests for the OpenAPI 3.0 emission and the route-metadata fluent API.

#include "atria/application.hpp"
#include "atria/json.hpp"
#include "atria/openapi.hpp"
#include "atria/request.hpp"
#include "atria/response.hpp"
#include "atria/router.hpp"
#include "atria/status.hpp"
#include "atria/websocket.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using atria::Application;
using atria::Json;
using atria::Request;
using atria::Response;
using atria::Status;

namespace {

struct CreateItemDto {
  std::string name;
  bool completed{};
};

struct ItemDto {
  std::int64_t id{};
  std::string name;
  bool completed{};
};

}  // namespace

template <> struct atria::openapi::schema_for<CreateItemDto> {
  static atria::Json get() {
    return atria::Json::object({
        {"type", std::string{"object"}},
        {"required", atria::Json::array({atria::Json{"name"}})},
        {"properties",
         atria::Json::object({
             {"name", atria::Json::object({{"type", std::string{"string"}}})},
             {"completed", atria::Json::object({{"type", std::string{"boolean"}}})},
         })},
    });
  }
};

template <> struct atria::openapi::schema_for<ItemDto> {
  static atria::Json get() {
    return atria::Json::object({
        {"type", std::string{"object"}},
        {"properties",
         atria::Json::object({
             {"id", atria::Json::object({{"type", std::string{"integer"}}})},
             {"name", atria::Json::object({{"type", std::string{"string"}}})},
             {"completed", atria::Json::object({{"type", std::string{"boolean"}}})},
         })},
    });
  }
};

TEST_CASE("openapi_json emits info and paths sections", "[openapi]") {
  Application app;
  app.info("Atria Test", "1.2.3");
  app.get("/health", [](Request&) { return Response::empty(Status::Ok); }).name("getHealth");

  Json document = app.openapi_json();
  REQUIRE(document.is_object());
  REQUIRE(document.find("openapi") != nullptr);
  CHECK(document.find("openapi")->as_string() == "3.0.3");

  const auto* info = document.find("info");
  REQUIRE(info != nullptr);
  CHECK(info->find("title")->as_string() == "Atria Test");
  CHECK(info->find("version")->as_string() == "1.2.3");

  const auto* paths = document.find("paths");
  REQUIRE(paths != nullptr);
  const auto* health_path = paths->find("/health");
  REQUIRE(health_path != nullptr);
  const auto* get_op = health_path->find("get");
  REQUIRE(get_op != nullptr);
  CHECK(get_op->find("operationId")->as_string() == "getHealth");
}

TEST_CASE("openapi_json captures fluent metadata", "[openapi]") {
  Application app;
  app.post("/items", [](Request&) { return Response::empty(Status::Created); })
      .name("createItem")
      .summary("Create a new item")
      .description("Creates and returns a new item record.")
      .tag("items")
      .request_body<CreateItemDto>()
      .response<ItemDto>(201, "Item created")
      .response_empty(422, "Validation error");

  Json document = app.openapi_json();
  const auto* op = document.find("paths")->find("/items")->find("post");
  REQUIRE(op != nullptr);

  CHECK(op->find("operationId")->as_string() == "createItem");
  CHECK(op->find("summary")->as_string() == "Create a new item");
  CHECK(op->find("tags")->as_array().at(0).as_string() == "items");

  const auto* request_body = op->find("requestBody");
  REQUIRE(request_body != nullptr);
  CHECK(request_body->find("required")->as_bool() == true);
  const auto* request_schema =
      request_body->find("content")->find("application/json")->find("schema");
  REQUIRE(request_schema != nullptr);
  CHECK(request_schema->find("type")->as_string() == "object");

  const auto* responses = op->find("responses");
  REQUIRE(responses != nullptr);
  const auto* created = responses->find("201");
  REQUIRE(created != nullptr);
  CHECK(created->find("description")->as_string() == "Item created");
  CHECK(
      created->find("content")
          ->find("application/json")
          ->find("schema")
          ->find("type")
          ->as_string() == "object"
  );

  const auto* unprocessable = responses->find("422");
  REQUIRE(unprocessable != nullptr);
  CHECK(unprocessable->find("description")->as_string() == "Validation error");
  CHECK(unprocessable->find("content") == nullptr);  // response_empty
}

TEST_CASE("openapi_json extracts path parameters from URL templates", "[openapi]") {
  Application app;
  app.get(
         "/users/{user_id}/posts/{post_id}",
         [](Request&) { return Response::empty(Status::Ok); }
  ).name("getUserPost");

  Json document = app.openapi_json();
  const auto* op = document.find("paths")->find("/users/{user_id}/posts/{post_id}")->find("get");
  REQUIRE(op != nullptr);
  const auto* parameters = op->find("parameters");
  REQUIRE(parameters != nullptr);
  REQUIRE(parameters->is_array());
  REQUIRE(parameters->as_array().size() == 2);
  CHECK(parameters->as_array().at(0).find("name")->as_string() == "user_id");
  CHECK(parameters->as_array().at(0).find("in")->as_string() == "path");
  CHECK(parameters->as_array().at(0).find("required")->as_bool() == true);
  CHECK(parameters->as_array().at(1).find("name")->as_string() == "post_id");
}

TEST_CASE("openapi_json walks group-registered routes", "[openapi]") {
  Application app;
  app.group("/api/v1", [](atria::RouteGroup& api) {
    api.get("/items", [](Request&) { return Response::empty(Status::Ok); }).name("listItems");
    api.post(
           "/items",
           [](Request&) { return Response::empty(Status::Created); }
    ).name("createItem");
    api.get("/items/{id}", [](Request&) { return Response::empty(Status::Ok); }).name("getItem");
  });

  Json document = app.openapi_json();
  const auto* paths = document.find("paths");
  REQUIRE(paths != nullptr);
  REQUIRE(paths->find("/api/v1/items") != nullptr);
  REQUIRE(paths->find("/api/v1/items/{id}") != nullptr);
  CHECK(paths->find("/api/v1/items")->find("get")->find("operationId")->as_string() == "listItems");
  CHECK(
      paths->find("/api/v1/items")->find("post")->find("operationId")->as_string() == "createItem"
  );
}

TEST_CASE("openapi_json captures websocket route metadata", "[openapi]") {
  Application app;
  app.websocket("/ws/{room}", [](atria::WebSocketSession&) {})
      .name("connectRoom")
      .summary("Connect to a room")
      .tag("websocket");
  app.get("/ws/{room}", [](Request&) { return Response::empty(Status::Ok); }).name("roomInfo");

  Json document = app.openapi_json();
  const auto* path = document.find("paths")->find("/ws/{room}");
  REQUIRE(path != nullptr);

  const auto* websocket = path->find("x-atria-websocket");
  REQUIRE(websocket != nullptr);
  CHECK(websocket->find("x-atria-websocket")->as_bool() == true);
  CHECK(websocket->find("operationId")->as_string() == "connectRoom");
  CHECK(websocket->find("summary")->as_string() == "Connect to a room");
  CHECK(websocket->find("tags")->as_array().at(0).as_string() == "websocket");
  CHECK(
      websocket->find("responses")->find("101")->find("description")->as_string() ==
      "Switching Protocols"
  );
  REQUIRE(websocket->find("parameters") != nullptr);
  CHECK(websocket->find("parameters")->as_array().at(0).find("name")->as_string() == "room");

  const auto* get = path->find("get");
  REQUIRE(get != nullptr);
  CHECK(get->find("operationId")->as_string() == "roomInfo");
}

TEST_CASE("schema_for primitive specializations are correct", "[openapi]") {
  using atria::openapi::schema_for;
  CHECK(schema_for<bool>::get().find("type")->as_string() == "boolean");
  CHECK(schema_for<int>::get().find("type")->as_string() == "integer");
  CHECK(schema_for<std::int64_t>::get().find("type")->as_string() == "integer");
  CHECK(schema_for<double>::get().find("type")->as_string() == "number");
  CHECK(schema_for<std::string>::get().find("type")->as_string() == "string");
}
