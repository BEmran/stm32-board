
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <netinet/in.h>

namespace connection {

class UdpSocket {
public:
  UdpSocket();
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;
  UdpSocket(UdpSocket&&) noexcept;
  UdpSocket& operator=(UdpSocket&&) noexcept;

  bool bind_rx(const std::string& local_addr, uint16_t local_port, bool nonblocking=true);
  bool set_tx_destination(const std::string& ip, uint16_t port);

  bool send(const void* data, size_t len) const;
  bool try_recv(void* data, size_t len, size_t& out_nbytes) const;

private:
  int fd_ = -1;
  bool has_dst_ = false;
  ::sockaddr_in dst_{};
};

} // namespace connection
