#include <array>
#include <atomic>
#include <atria/atria.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view kSsdpGroup = "239.255.255.250";
constexpr std::uint16_t kSsdpPort = 1900;

[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::string device_description_xml(std::string_view host) {
  std::string xml;
  xml.append(R"(<?xml version="1.0"?>)");
  xml.append(R"(<root xmlns="urn:schemas-upnp-org:device-1-0">)");
  xml.append("<specVersion><major>1</major><minor>0</minor></specVersion>");
  xml.append("<URLBase>http://");
  xml.append(host);
  xml.append(":8080/</URLBase>");
  xml.append("<device>");
  xml.append("<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>");
  xml.append("<friendlyName>Atria UPnP Skeleton</friendlyName>");
  xml.append("<manufacturer>Atria</manufacturer>");
  xml.append("<modelName>Atria UPnP Skeleton</modelName>");
  xml.append("<UDN>uuid:atria-upnp-skeleton</UDN>");
  xml.append("</device></root>");
  return xml;
}

void run_ssdp_responder(std::atomic<bool>& running, std::string host) {
  auto socket = atria::UdpSocket::bind_ipv4("0.0.0.0", kSsdpPort);
  if (!socket.has_value()) {
    return;
  }
  (void)socket->set_receive_timeout(1000);
  (void)socket->join_ipv4_multicast(kSsdpGroup);

  std::array<char, 2048> buffer{};
  while (running.load()) {
    auto received = socket->receive_from(buffer);
    if (!received.has_value()) {
      continue;
    }
    std::string_view request{buffer.data(), received->bytes_received};
    if (!request.starts_with("M-SEARCH ") || !contains(request, "ssdp:discover")) {
      continue;
    }

    std::string response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("CACHE-CONTROL: max-age=1800\r\n");
    response.append("EXT:\r\n");
    response.append("LOCATION: http://");
    response.append(host);
    response.append(":8080/description.xml\r\n");
    response.append("SERVER: Atria/0.1 UPnP/1.0 DLNADOC/1.50\r\n");
    response.append("ST: urn:schemas-upnp-org:device:MediaServer:1\r\n");
    response.append("USN: uuid:atria-upnp-skeleton::urn:schemas-upnp-org:device:MediaServer:1\r\n");
    response.append("\r\n");
    (void)socket->send_to(response, received->remote);
  }

  (void)socket->leave_ipv4_multicast(kSsdpGroup);
}

}  // namespace

int main() {
  auto selected = atria::select_lan_ipv4_interface();
  std::string host = "127.0.0.1";
  if (selected.has_value() && selected->has_value()) {
    host = selected->value().ipv4_address;
  }

  std::atomic<bool> running{true};
  std::thread ssdp_thread{[&] { run_ssdp_responder(running, host); }};

  atria::Application app;
  app.get("/description.xml", [host](atria::Request&) {
    return atria::Response::xml(device_description_xml(host));
  });

  atria::ServerConfig config;
  config.host = "0.0.0.0";
  config.port = 8080;
  config.worker_threads = 2;
  const int result = app.listen(config);

  running.store(false);
  if (ssdp_thread.joinable()) {
    ssdp_thread.join();
  }
  return result;
}
