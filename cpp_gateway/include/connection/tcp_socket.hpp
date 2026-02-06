#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <netinet/in.h>

namespace connection {

class TcpSocket {
public:
  TcpSocket();
  ~TcpSocket();

  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  TcpSocket(TcpSocket&&) noexcept;
  TcpSocket& operator=(TcpSocket&&) noexcept;

  bool is_open() const { return fd_ >= 0; }

  bool connect_to(const std::string& ip, uint16_t port, bool nonblocking=false);
  bool bind_listen(const std::string& local_addr, uint16_t local_port, int backlog=1);
  bool accept_client(TcpSocket& out, bool nonblocking=false);

  bool send_all(const void* data, size_t len) const;
  bool recv_all(void* data, size_t len) const;
  bool try_recv(void* data, size_t len, size_t& out_nbytes) const;

  void close();

private:
  int fd_ = -1;
};

} // namespace connection
