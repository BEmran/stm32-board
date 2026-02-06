#include "connection/tcp_socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace connection {

static bool set_nonblocking(int fd, bool on) {
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

TcpSocket::~TcpSocket() {
  close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept {
  fd_ = other.fd_;
  other.fd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this == &other) return *this;
  close();
  fd_ = other.fd_;
  other.fd_ = -1;
  return *this;
}

void TcpSocket::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool TcpSocket::connect_to(const std::string& ip, uint16_t port, bool nonblocking) {
  if (fd_ < 0) return false;

  ::sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return false;

  if (nonblocking) {
    if (!set_nonblocking(fd_, true)) return false;
  }

  if (::connect(fd_, (sockaddr*)&addr, sizeof(addr)) == 0) return true;

  if (nonblocking && errno == EINPROGRESS) return true;
  return false;
}

bool TcpSocket::bind_listen(const std::string& local_addr, uint16_t local_port, int backlog) {
  if (fd_ < 0) return false;

  int reuse = 1;
  (void)setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  ::sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(local_port);
  if (inet_pton(AF_INET, local_addr.c_str(), &addr.sin_addr) != 1) return false;

  if (::bind(fd_, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
  return ::listen(fd_, backlog) == 0;
}

bool TcpSocket::accept_client(TcpSocket& out, bool nonblocking) {
  if (fd_ < 0) return false;

  int cfd = ::accept(fd_, nullptr, nullptr);
  if (cfd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
    return false;
  }

  if (nonblocking) {
    if (!set_nonblocking(cfd, true)) {
      ::close(cfd);
      return false;
    }
  }

  out.close();
  out.fd_ = cfd;
  return true;
}

bool TcpSocket::send_all(const void* data, size_t len) const {
  if (fd_ < 0) return false;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  size_t sent = 0;
  while (sent < len) {
    const ssize_t n = ::send(fd_, p + sent, len - sent, 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
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
    if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
    return false;
  }
  out_nbytes = static_cast<size_t>(n);
  return true;
}

} // namespace connection
