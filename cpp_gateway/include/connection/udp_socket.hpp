
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <netinet/in.h>

namespace connection
{

  class UdpSocket
  {
  public:
    UdpSocket();
    ~UdpSocket() noexcept;

    UdpSocket(const UdpSocket &) = delete;
    UdpSocket &operator=(const UdpSocket &) = delete;
    UdpSocket(UdpSocket &&) noexcept;
    UdpSocket &operator=(UdpSocket &&) noexcept;

    [[nodiscard]] bool bind_rx(std::string_view local_addr, uint16_t local_port, bool nonblocking = true);
    [[nodiscard]] bool set_tx_destination(std::string_view ip, uint16_t port);

    [[nodiscard]] bool send(const void *data, size_t len) const;
    [[nodiscard]] bool try_recv(void *data, size_t len, size_t &out_nbytes) const;
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

  private:
    int fd_ = -1;
    bool has_dst_ = false;
    ::sockaddr_in dst_{};
  };

} // namespace connection
