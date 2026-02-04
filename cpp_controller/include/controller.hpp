#pragma once
#include "packets.hpp"
#include "udp_socket.hpp"

#include <cstdint>
#include <string>

namespace app {

struct ControllerConfig {
  std::string ip = "127.0.0.1";
  uint16_t state_port = 20001; // RX from gateway
  uint16_t cmd_port   = 20002; // TX to gateway
  double hz = 100.0;
  double print_period_s = 1.0;
};

class Controller {
public:
  explicit Controller(const ControllerConfig& cfg);

  bool init();
  int run(); // blocks forever

private:
  ControllerConfig cfg_;
  net::UdpSocket rx_;
  net::UdpSocket tx_;

  StatePkt last_state_{};
  bool have_state_ = false;

  uint32_t cmd_seq_ = 0;
};

} // namespace app
