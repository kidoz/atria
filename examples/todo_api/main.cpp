#include <atria/atria.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TodoItem {
  std::int64_t id{0};
  std::string name;
  bool completed{false};
};

class TodoStore {
 public:
  std::vector<TodoItem> list() {
    std::lock_guard<std::mutex> lock(mu_);
    return items_;
  }

  TodoItem create(std::string name, bool completed) {
    std::lock_guard<std::mutex> lock(mu_);
    TodoItem item{++next_id_, std::move(name), completed};
    items_.push_back(item);
    return item;
  }

  bool update(std::int64_t id, const std::string& name, bool completed) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& item : items_) {
      if (item.id == id) {
        item.name = name;
        item.completed = completed;
        return true;
      }
    }
    return false;
  }

  bool remove(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto it = items_.begin(); it != items_.end(); ++it) {
      if (it->id == id) {
        items_.erase(it);
        return true;
      }
    }
    return false;
  }

  TodoItem* find(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& item : items_) {
      if (item.id == id) {
        return &item;
      }
    }
    return nullptr;
  }

 private:
  std::mutex mu_;
  std::int64_t next_id_{0};
  std::vector<TodoItem> items_;
};

atria::Json item_to_json(const TodoItem& item) {
  return atria::Json::object({
      {"id", item.id},
      {"name", item.name},
      {"completed", item.completed},
  });
}

std::int64_t parse_id(std::string_view s) {
  std::int64_t out = 0;
  for (char c : s) {
    if (c < '0' || c > '9') {
      return -1;
    }
    out = out * 10 + (c - '0');
  }
  return out;
}

}  // namespace

int main() {
  atria::Application app;
  TodoStore store;

  app.info("Atria Todo API", "0.1.0");
  app.use(atria::middleware::error_handler());
  app.use(atria::middleware::request_logger());

  app.get("/health", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"status", "ok"}}));
  })
      .name("getHealth")
      .summary("Liveness check");

  app.get("/openapi.json", [&](atria::Request&) {
    return atria::Response::json(app.openapi_json());
  })
      .name("getOpenApiDocument")
      .summary("OpenAPI 3.0 description of this service");

  app.group("/api/v1", [&](atria::RouteGroup& api) {
    api.get("/items", [&](atria::Request&) {
      atria::Json::Array arr;
      for (const auto& item : store.list()) {
        arr.push_back(item_to_json(item));
      }
      return atria::Response::json(atria::Json{std::move(arr)});
    });

    api.post("/items", [&](atria::Request& request) {
      auto parsed = atria::Json::parse(request.body());
      if (!parsed.has_value()) {
        return atria::Response::json(
            atria::Json::object({{"error", atria::Json::object({
                                               {"code", "bad_request"},
                                               {"message", "Invalid JSON"},
                                           })}}),
            atria::Status::BadRequest);
      }
      atria::Validator v;
      v.string_required("name", *parsed).string_max("name", *parsed, 100);
      v.boolean_optional("completed", *parsed);
      if (!v.ok()) {
        return atria::Response::json(
            atria::to_error_json("validation_error", "Request validation failed", v.error()),
            atria::Status::UnprocessableEntity);
      }
      bool completed = false;
      if (auto* c = parsed->find("completed"); c != nullptr && c->is_bool()) {
        completed = c->as_bool();
      }
      auto created = store.create(parsed->find("name")->as_string(), completed);
      return atria::Response::json(item_to_json(created), atria::Status::Created);
    });

    api.get("/items/{id}", [&](atria::Request& request) {
      std::int64_t id = parse_id(request.path_param("id").value_or(""));
      if (id < 0) {
        return atria::Response::json(
            atria::Json::object({{"error", atria::Json::object({
                                               {"code", "bad_request"},
                                               {"message", "Invalid id"},
                                           })}}),
            atria::Status::BadRequest);
      }
      auto* item = store.find(id);
      if (item == nullptr) {
        return atria::Response::json(
            atria::Json::object({{"error", atria::Json::object({
                                               {"code", "not_found"},
                                               {"message", "Item not found"},
                                           })}}),
            atria::Status::NotFound);
      }
      return atria::Response::json(item_to_json(*item));
    });

    api.put("/items/{id}", [&](atria::Request& request) {
      std::int64_t id = parse_id(request.path_param("id").value_or(""));
      auto parsed = atria::Json::parse(request.body());
      if (id < 0 || !parsed.has_value()) {
        return atria::Response::empty(atria::Status::BadRequest);
      }
      atria::Validator v;
      v.string_required("name", *parsed).boolean_optional("completed", *parsed);
      if (!v.ok()) {
        return atria::Response::json(
            atria::to_error_json("validation_error", "Request validation failed", v.error()),
            atria::Status::UnprocessableEntity);
      }
      bool completed = false;
      if (auto* c = parsed->find("completed"); c != nullptr && c->is_bool()) {
        completed = c->as_bool();
      }
      if (!store.update(id, parsed->find("name")->as_string(), completed)) {
        return atria::Response::empty(atria::Status::NotFound);
      }
      return atria::Response::empty();
    });

    api.del("/items/{id}", [&](atria::Request& request) {
      std::int64_t id = parse_id(request.path_param("id").value_or(""));
      if (id < 0 || !store.remove(id)) {
        return atria::Response::empty(atria::Status::NotFound);
      }
      return atria::Response::empty();
    });
  });

  atria::ServerConfig cfg;
  cfg.host = "0.0.0.0";
  cfg.port = 8080;
  cfg.worker_threads = 4;  // dispatch handlers off the loop thread
  return app.listen(cfg);
}
