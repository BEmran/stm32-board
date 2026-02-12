#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <netinet/in.h>

namespace connection
{

  class TcpSocket
  {
  public:
    TcpSocket();
    ~TcpSocket() noexcept;

    TcpSocket(const TcpSocket &) = delete;
    TcpSocket &operator=(const TcpSocket &) = delete;
    TcpSocket(TcpSocket &&) noexcept;
    TcpSocket &operator=(TcpSocket &&) noexcept;

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    [[nodiscard]] bool connect_to(std::string_view ip, uint16_t port, bool nonblocking = false);
    [[nodiscard]] bool bind_listen(std::string_view local_addr, uint16_t local_port, int backlog = 1);
    [[nodiscard]] bool accept_client(TcpSocket &out, bool nonblocking = false);
    [[nodiscard]] bool set_nonblocking(bool on = true);

    [[nodiscard]] bool send_all(const void *data, size_t len) const;
    [[nodiscard]] bool recv_all(void *data, size_t len) const;
    [[nodiscard]] bool try_recv(void *data, size_t len, size_t &out_nbytes) const;

    void close() noexcept;

  private:
    int fd_ = -1;
  };

} // namespace connection
