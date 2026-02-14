#include "connection/tcp_socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

namespace connection {

static bool set_nonblocking_fd(int fd, bool on) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  if (on) {
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
  }
  return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
}

TcpSocket::TcpSocket() {
  fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
}

TcpSocket::~TcpSocket() noexcept {
  close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept {
  fd_ = std::exchange(other.fd_, -1);
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this == &other) return *this;
  close();
  fd_ = std::exchange(other.fd_, -1);
  return *this;
}

void TcpSocket::close() noexcept {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool TcpSocket::connect_to(std::string_view ip, uint16_t port, bool nonblocking) {
  if (fd_ < 0) return false;

  ::sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  const std::string ip_str(ip);
  if (inet_pton(AF_INET, ip_str.c_str(), &addr.sin_addr) != 1) return false;

  if (nonblocking) {
    if (!set_nonblocking_fd(fd_, true)) return false;
  }

  if (::connect(fd_, (sockaddr*)&addr, sizeof(addr)) == 0) return true;

  if (nonblocking && errno == EINPROGRESS) return true;
  return false;
}

bool TcpSocket::bind_listen(std::string_view local_addr, uint16_t local_port, int backlog) {
  if (fd_ < 0) return false;

  int reuse = 1;
  (void)setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  ::sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(local_port);
  const std::string ip_str(local_addr);
  if (inet_pton(AF_INET, ip_str.c_str(), &addr.sin_addr) != 1) return false;

  if (::bind(fd_, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
  return ::listen(fd_, backlog) == 0;
}

bool TcpSocket::accept_client(TcpSocket& out, bool nonblocking) {
  if (fd_ < 0) return false;

  int cfd = -1;
  for (;;) {
    cfd = ::accept(fd_, nullptr, nullptr);
    if (cfd >= 0) break;
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
    return false;
  }

  if (nonblocking) {
    if (!set_nonblocking_fd(cfd, true)) {
      ::close(cfd);
      return false;
    }
  }

  out.close();
  out.fd_ = cfd;
  return true;
}

bool TcpSocket::set_nonblocking(bool on) {
  if (fd_ < 0) return false;
  return set_nonblocking_fd(fd_, on);
}

bool TcpSocket::send_all(const void* data, size_t len) const {
  if (fd_ < 0) return false;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  size_t sent = 0;
  while (sent < len) {
    const int flags =
#ifdef MSG_NOSIGNAL
      MSG_NOSIGNAL;
#else
      0;
#endif
    const ssize_t n = ::send(fd_, p + sent, len - sent, flags);
    if (n > 0) {
      sent += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // wait until writable
      ::pollfd fds{};
      fds.fd = fd_;
      fds.events = POLLOUT;
      const int rc = ::poll(&fds, 1, 50);
      if (rc <= 0) return false;
      continue;
    }
    return false; // other error
  }
  return true;
}


bool TcpSocket::try_send(const void* data, size_t len, size_t& out_nbytes) const {
  out_nbytes = 0;
  if (fd_ < 0) return false;
  const int flags =
#ifdef MSG_NOSIGNAL
    MSG_NOSIGNAL;
#else
    0;
#endif
  const ssize_t n = ::send(fd_, data, len, flags);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      out_nbytes = 0;
      return true; // would block, try later
    }
    return false;
  }
  if (n == 0) return false;
  out_nbytes = static_cast<size_t>(n);
  return true;
}

bool TcpSocket::recv_all(void* data, size_t len) const {
  if (fd_ < 0) return false;
  uint8_t* p = static_cast<uint8_t*>(data);
  size_t recvd = 0;
  while (recvd < len) {
    const ssize_t n = ::recv(fd_, p + recvd, len - recvd, 0);
    if (n <= 0) return false;
    recvd += static_cast<size_t>(n);
  }
  return true;
}

bool TcpSocket::try_recv(void* data, size_t len, size_t& out_nbytes) const {
  out_nbytes = 0;
  if (fd_ < 0) return false;
  const ssize_t n = ::recv(fd_, data, len, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Non-blocking socket: no data available right now.
      out_nbytes = 0;
      return true;
    }
    if (errno == EINTR) {
      out_nbytes = 0;
      return true;
    }
    return false;
  }
  if (n == 0) {
    // Peer closed.
    out_nbytes = 0;
    return false;
  }
  out_nbytes = static_cast<size_t>(n);
  return true;
}

} // namespace connection
