#include "udp_socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace net {

static bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

UdpSocket::UdpSocket() {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
}

UdpSocket::~UdpSocket() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept {
  fd_ = other.fd_;
  other.fd_ = -1;
  has_dst_ = other.has_dst_;
  dst_ = other.dst_;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
  if (this == &other) return *this;
  if (fd_ >= 0) ::close(fd_);
  fd_ = other.fd_;
  other.fd_ = -1;
  has_dst_ = other.has_dst_;
  dst_ = other.dst_;
  return *this;
}

bool UdpSocket::bind_rx(uint16_t local_port, bool nonblocking) {
  if (fd_ < 0) return false;

  int reuse = 1;
  (void)setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(local_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (::bind(fd_, (sockaddr*)&addr, sizeof(addr)) != 0) return false;

  if (nonblocking) {
    if (!set_nonblocking(fd_)) return false;
  }
  return true;
}

bool UdpSocket::set_tx_destination(const std::string& ip, uint16_t port) {
  if (fd_ < 0) return false;

  std::memset(&dst_, 0, sizeof(dst_));
  dst_.sin_family = AF_INET;
  dst_.sin_port = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &dst_.sin_addr) != 1) return false;

  has_dst_ = true;
  return true;
}

bool UdpSocket::send(const void* data, size_t len) const {
  if (fd_ < 0 || !has_dst_) return false;
  ssize_t n = ::sendto(fd_, data, len, 0, (const sockaddr*)&dst_, sizeof(dst_));
  return n == (ssize_t)len;
}

bool UdpSocket::try_recv(void* data, size_t len, size_t& out_nbytes) const {
  out_nbytes = 0;
  if (fd_ < 0) return false;

  ssize_t n = ::recvfrom(fd_, data, len, 0, nullptr, nullptr);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
    return false;
  }
  out_nbytes = (size_t)n;
  return true;
}

} // namespace net
