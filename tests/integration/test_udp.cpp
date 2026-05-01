#include "atria/network_interface.hpp"
#include "atria/udp.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("UDP sockets send and receive on loopback", "[udp]") {
  auto receiver = atria::UdpSocket::bind_ipv4("127.0.0.1", 0);
  REQUIRE(receiver.has_value());
  REQUIRE(receiver->set_receive_timeout(1000).has_value());
  auto port = receiver->local_port();
  REQUIRE(port.has_value());

  auto sender = atria::UdpSocket::open_ipv4();
  REQUIRE(sender.has_value());
  constexpr std::string_view message = "ssdp probe";
  auto sent = sender->send_to(message, atria::UdpEndpoint{.address = "127.0.0.1", .port = *port});
  REQUIRE(sent.has_value());
  CHECK(*sent == message.size());

  std::array<char, 128> buffer{};
  auto received = receiver->receive_from(buffer);
  REQUIRE(received.has_value());
  CHECK(received->bytes_received == message.size());
  CHECK(std::string_view{buffer.data(), received->bytes_received} == message);
  CHECK(received->remote.address == "127.0.0.1");
}

TEST_CASE("network interface enumeration is callable", "[network-interface]") {
  auto interfaces = atria::enumerate_ipv4_interfaces();
  REQUIRE(interfaces.has_value());
}
