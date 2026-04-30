#pragma once

#include "atria/request.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace atria::net {
class Connection;
}

namespace atria {

enum class WebSocketCloseCode : std::uint16_t {
  NormalClosure = 1000,
  ProtocolError = 1002,
  InvalidFramePayloadData = 1007,
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

  [[nodiscard]] const std::vector<std::string>& requested_subprotocols() const noexcept {
    return requested_subprotocols_;
  }

  [[nodiscard]] std::string_view selected_subprotocol() const noexcept {
    return selected_subprotocol_;
  }

  [[nodiscard]] bool select_subprotocol(std::string_view protocol);

  void on_text(TextHandler handler);
  void on_binary(BinaryHandler handler);
  void on_close(CloseHandler handler);

  void send_text(std::string message);
  void send_binary(std::string message);
  void close(WebSocketCloseCode code = WebSocketCloseCode::NormalClosure, std::string reason = {});

  class Sender {
  public:
    void send_text(std::string message) const;
    void send_binary(std::string message) const;
    void close(
        WebSocketCloseCode code = WebSocketCloseCode::NormalClosure,
        std::string reason = {}
    ) const;

  private:
    friend class WebSocketSession;
    friend class net::Connection;

    void set_text_sender(std::function<void(std::string)> sender);
    void set_binary_sender(std::function<void(std::string)> sender);
    void set_close_sender(std::function<void(WebSocketCloseCode, std::string)> sender);

    std::function<void(std::string)> text_sender_;
    std::function<void(std::string)> binary_sender_;
    std::function<void(WebSocketCloseCode, std::string)> close_sender_;
  };

  [[nodiscard]] Sender sender() const { return sender_; }

private:
  friend class net::Connection;
  friend class Application;

  void set_request(const Request* request) noexcept { request_ = request; }

  void set_requested_subprotocols(std::vector<std::string> protocols);

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
  Sender sender_;
  std::vector<std::string> requested_subprotocols_;
  std::string selected_subprotocol_;
};

using WebSocketHandler = std::function<void(WebSocketSession&)>;
using WebSocketSender = WebSocketSession::Sender;

}  // namespace atria
