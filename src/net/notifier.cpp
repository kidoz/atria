#include "net/notifier.hpp"

#include "platform/socket.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <memory>

namespace atria::net {

namespace {

#if !defined(_WIN32)
[[nodiscard]] int set_cloexec_nonblock(int fd) noexcept {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return -1;
  }
  int fdflags = ::fcntl(fd, F_GETFD, 0);
  if (fdflags < 0) {
    return -1;
  }
  (void)::fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
  return 0;
}
#endif

}  // namespace

Notifier::Notifier() = default;

Notifier::~Notifier() {
#if defined(_WIN32)
  // SocketHandle destructors close the sockets.
#else
  if (read_fd_ >= 0) {
    ::close(read_fd_);
  }
  if (write_fd_ >= 0) {
    ::close(write_fd_);
  }
#endif
}

std::unique_ptr<Notifier> Notifier::create() {
  std::unique_ptr<Notifier> n{new Notifier};

#if defined(_WIN32)
  platform::global_init();

  // Build a self-connected loopback TCP pair.
  SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) {
    return nullptr;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    ::closesocket(listener);
    return nullptr;
  }
  int alen = sizeof(addr);
  if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &alen) == SOCKET_ERROR) {
    ::closesocket(listener);
    return nullptr;
  }
  if (::listen(listener, 1) == SOCKET_ERROR) {
    ::closesocket(listener);
    return nullptr;
  }
  SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (client == INVALID_SOCKET) {
    ::closesocket(listener);
    return nullptr;
  }
  if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    ::closesocket(listener);
    ::closesocket(client);
    return nullptr;
  }
  SOCKET server = ::accept(listener, nullptr, nullptr);
  ::closesocket(listener);
  if (server == INVALID_SOCKET) {
    ::closesocket(client);
    return nullptr;
  }
  // The server end is our read fd; the client end is our write fd.
  n->read_ = platform::SocketHandle{server};
  n->write_ = platform::SocketHandle{client};
  // Make the read end non-blocking so drain() never stalls.
  u_long mode = 1;
  ::ioctlsocket(server, FIONBIO, &mode);
#else
  int pipefds[2] = {-1, -1};
  if (::pipe(pipefds) < 0) {
    return nullptr;
  }
  if (set_cloexec_nonblock(pipefds[0]) < 0 || set_cloexec_nonblock(pipefds[1]) < 0) {
    ::close(pipefds[0]);
    ::close(pipefds[1]);
    return nullptr;
  }
  n->read_fd_ = pipefds[0];
  n->write_fd_ = pipefds[1];
#endif
  return n;
}

platform::NativeSocket Notifier::read_fd() const noexcept {
#if defined(_WIN32)
  return read_.native();
#else
  return read_fd_;
#endif
}

void Notifier::notify() {
  const char byte = 1;
#if defined(_WIN32)
  (void)::send(write_.native(), &byte, 1, 0);
#else
  // EAGAIN is fine — the pipe already has a pending byte; a single byte is enough to wake.
  (void)::write(write_fd_, &byte, 1);
#endif
}

void Notifier::drain() {
  char buf[64];
#if defined(_WIN32)
  while (true) {
    int n = ::recv(read_.native(), buf, sizeof(buf), 0);
    if (n <= 0) {
      break;
    }
  }
#else
  while (true) {
    ssize_t n = ::read(read_fd_, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
  }
#endif
}

}  // namespace atria::net
