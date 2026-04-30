#pragma once

#include "atria/request.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace atria::net {
class Connection;
}

namespace atria {

enum class WebSocketCloseCode : std::uint16_t {
  NormalClosure = 1000,
  ProtocolError = 1002,
  MessageTooBig = 1009,
  InternalError = 1011,
};

class WebSocketSession {
public:
  using TextHandler = std::function<void(WebSocketSession&, std::string_view)>;
  using BinaryHandler = std::function<void(WebSocketSession&, std::string_view)>;
  using CloseHandler = std::function<void(WebSocketSession&, WebSocketCloseCode)>;

  WebSocketSession() = default;

  [[nodiscard]] const Request& request() const noexcept { return *request_; }

  void on_text(TextHandler handler);
  void on_binary(BinaryHandler handler);
  void on_close(CloseHandler handler);

  void send_text(std::string message);
  void send_binary(std::string message);
  void close(WebSocketCloseCode code = WebSocketCloseCode::NormalClosure, std::string reason = {});

private:
  friend class net::Connection;
  friend class Application;

  void set_request(const Request* request) noexcept { request_ = request; }

  void set_text_sender(std::function<void(std::string)> sender);
  void set_binary_sender(std::function<void(std::string)> sender);
  void set_close_sender(std::function<void(WebSocketCloseCode, std::string)> sender);

  void receive_text(std::string_view message);
  void receive_binary(std::string_view message);
  void receive_close(WebSocketCloseCode code);

  const Request* request_{nullptr};
  TextHandler text_handler_;
  BinaryHandler binary_handler_;
  CloseHandler close_handler_;
  std::function<void(std::string)> text_sender_;
  std::function<void(std::string)> binary_sender_;
  std::function<void(WebSocketCloseCode, std::string)> close_sender_;
};

using WebSocketHandler = std::function<void(WebSocketSession&)>;

}  // namespace atria
