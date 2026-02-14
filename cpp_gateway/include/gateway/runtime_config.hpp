#pragma once
#include "gateway/enums.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace gateway {

struct RuntimeConfig {
  // Rates
  double usb_hz{200.0};
  double tcp_hz{200.0};
  double ctrl_hz{200.0};

  // Networking
  std::string bind_ip{"0.0.0.0"};
  uint16_t state_port{30001};
  uint16_t cmd_port{30002};

  // Serial
  std::string serial_dev{"/dev/ttyUSB0"};
  int serial_baud{115200};

  // Safety
  double cmd_timeout_s{0.2};
  UsbTimeoutMode usb_timeout_mode{UsbTimeoutMode::ENFORCE};

  // Control
  ControlMode control_mode{ControlMode::PASS_THROUGH_CMD};
  int16_t ctrl_thread_priority{0}; // Linux: SCHED_FIFO priority (1..99). 0 disables.

  // Logging
  bool binary_log{true};
  std::string log_path{"./logs/gateway.bin"};
  uint32_t log_rotate_mb{256};   // 0 disables rotation
  uint32_t log_rotate_keep{10};  // number of rotated files to keep (best-effort)

  // Flags routing
  uint8_t flag_event_mask{0x07};
  int flag_start_bit{-1};
  int flag_stop_bit{-1};
  int flag_reset_bit{-1};
};

using RuntimeConfigPtr = std::shared_ptr<const RuntimeConfig>;

} // namespace gateway
