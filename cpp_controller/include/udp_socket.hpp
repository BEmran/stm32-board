#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <netinet/in.h>

namespace net {

class UdpSocket {
public:
  UdpSocket();
  ~UdpSocket();

  // Non-copyable
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  // Movable
  UdpSocket(UdpSocket&&) noexcept;
  UdpSocket& operator=(UdpSocket&&) noexcept;

  // Bind to local port for receiving.
  // If nonblocking=true, recv will return false when no data available.
  bool bind_rx(uint16_t local_port, bool nonblocking = true);

  // Set a default destination for send().
  bool set_tx_destination(const std::string& ip, uint16_t port);

  // Send to the configured destination.
  bool send(const void* data, size_t len) const;

  // Try to receive up to len bytes. Returns true if a datagram was received.
  // out_nbytes is set to actual datagram size (may differ from len).
  bool try_recv(void* data, size_t len, size_t& out_nbytes) const;

  int fd() const { return fd_; }

private:
  int fd_ = -1;

  // destination (for send)
  bool has_dst_ = false;
  struct sockaddr_in dst_;
};

} // namespace net
