#include <atomic>
#include <atria/atria.hpp>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kIndexHtml = R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Atria WebSocket Chat</title>
  <style>
    body { font: 16px system-ui, sans-serif; max-width: 760px; margin: 40px auto; padding: 0 16px; }
    #log { border: 1px solid #ccc; min-height: 280px; padding: 12px; white-space: pre-wrap; }
    form { display: flex; gap: 8px; margin-top: 12px; }
    input { flex: 1; padding: 8px; }
    button { padding: 8px 12px; }
  </style>
</head>
<body>
  <h1>Atria WebSocket Chat</h1>
  <div id="log"></div>
  <form id="form">
    <input id="message" autocomplete="off" placeholder="Message">
    <button>Send</button>
  </form>
  <script>
    const log = document.getElementById("log");
    const message = document.getElementById("message");
    const socket = new WebSocket(`ws://${location.host}/ws/lobby`);
    socket.onmessage = (event) => { log.textContent += event.data + "\n"; };
    socket.onclose = () => { log.textContent += "closed\n"; };
    document.getElementById("form").addEventListener("submit", (event) => {
      event.preventDefault();
      if (message.value) {
        socket.send(message.value);
        message.value = "";
      }
    });
  </script>
</body>
</html>
)";

struct ChatClient {
  std::uint64_t id{0};
  std::string room;
  atria::WebSocketSender sender;
};

class ChatHub {
public:
  [[nodiscard]] std::uint64_t join(std::string room, atria::WebSocketSender sender) {
    ChatClient client{
        .id = next_id_.fetch_add(1),
        .room = std::move(room),
        .sender = std::move(sender),
    };
    auto id = client.id;
    std::scoped_lock lock(mutex_);
    clients_.push_back(std::move(client));
    return id;
  }

  void leave(std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    std::erase_if(clients_, [id](const ChatClient& client) { return client.id == id; });
  }

  void broadcast(std::string_view room, const std::string& message) {
    std::vector<atria::WebSocketSender> senders;
    {
      std::scoped_lock lock(mutex_);
      for (const auto& client : clients_) {
        if (client.room == room) {
          senders.push_back(client.sender);
        }
      }
    }
    for (const auto& sender : senders) {
      sender.send_text(message);
    }
  }

private:
  std::atomic<std::uint64_t> next_id_{1};
  std::mutex mutex_;
  std::vector<ChatClient> clients_;
};

int run_app() {
  atria::Application app;
  ChatHub hub;

  app.use(atria::middleware::error_handler());

  app.get("/", [](atria::Request&) {
    atria::Response response = atria::Response::text(std::string{kIndexHtml});
    response.set_header("Content-Type", "text/html; charset=utf-8");
    return response;
  });

  app.get("/health", [](atria::Request&) {
    return atria::Response::json(atria::Json::object({{"status", "ok"}}));
  });

  app.websocket("/ws/{room}", [&hub](atria::WebSocketSession& session) {
    std::string room{session.request().path_param("room").value_or("lobby")};
    auto sender = session.sender();
    auto id = hub.join(room, sender);

    sender.send_text("joined " + room);
    hub.broadcast(room, "client " + std::to_string(id) + " joined");

    session.on_text([&hub, id, room](atria::WebSocketSession&, std::string_view message) {
      hub.broadcast(room, "client " + std::to_string(id) + ": " + std::string{message});
    });
    session.on_close([&hub, id, room](atria::WebSocketSession&, atria::WebSocketCloseCode) {
      hub.leave(id);
      hub.broadcast(room, "client " + std::to_string(id) + " left");
    });
  });

  atria::ServerConfig config;
  config.host = "0.0.0.0";
  config.port = 8082;
  config.worker_threads = 4;
  return app.listen(config);
}

}  // namespace

int main() {
  try {
    return run_app();
  } catch (const std::exception& err) {
    std::cerr << "atria-websocket-chat failed: " << err.what() << '\n';
  } catch (...) {
    std::cerr << "atria-websocket-chat failed: unknown error\n";
  }
  return 1;
}
