#include "atria/websocket.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace atria {

void WebSocketSession::on_text(TextHandler handler) {
  text_handler_ = std::move(handler);
}

void WebSocketSession::on_binary(BinaryHandler handler) {
  binary_handler_ = std::move(handler);
}

void WebSocketSession::on_close(CloseHandler handler) {
  close_handler_ = std::move(handler);
}

void WebSocketSession::send_text(std::string message) {
  sender_.send_text(std::move(message));
}

void WebSocketSession::send_binary(std::string message) {
  sender_.send_binary(std::move(message));
}

void WebSocketSession::close(WebSocketCloseCode code, std::string reason) {
  sender_.close(code, std::move(reason));
}

void WebSocketSession::set_text_sender(std::function<void(std::string)> sender) {
  sender_.set_text_sender(std::move(sender));
}

void WebSocketSession::set_binary_sender(std::function<void(std::string)> sender) {
  sender_.set_binary_sender(std::move(sender));
}

void WebSocketSession::set_close_sender(
    std::function<void(WebSocketCloseCode, std::string)> sender
) {
  sender_.set_close_sender(std::move(sender));
}

void WebSocketSession::Sender::send_text(std::string message) const {
  if (text_sender_) {
    text_sender_(std::move(message));
  }
}

void WebSocketSession::Sender::send_binary(std::string message) const {
  if (binary_sender_) {
    binary_sender_(std::move(message));
  }
}

void WebSocketSession::Sender::close(WebSocketCloseCode code, std::string reason) const {
  if (close_sender_) {
    close_sender_(code, std::move(reason));
  }
}

void WebSocketSession::Sender::set_text_sender(std::function<void(std::string)> sender) {
  text_sender_ = std::move(sender);
}

void WebSocketSession::Sender::set_binary_sender(std::function<void(std::string)> sender) {
  binary_sender_ = std::move(sender);
}

void WebSocketSession::Sender::set_close_sender(
    std::function<void(WebSocketCloseCode, std::string)> sender
) {
  close_sender_ = std::move(sender);
}

void WebSocketSession::receive_text(std::string_view message) {
  if (text_handler_) {
    text_handler_(*this, message);
  }
}

void WebSocketSession::receive_binary(std::string_view message) {
  if (binary_handler_) {
    binary_handler_(*this, message);
  }
}

void WebSocketSession::receive_close(WebSocketCloseCode code) {
  if (close_handler_) {
    close_handler_(*this, code);
  }
}

}  // namespace atria
