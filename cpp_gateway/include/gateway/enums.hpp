#pragma once
#include <cstdint>

namespace gateway {

enum class ControlMode : uint8_t {
  PASS_THROUGH_CMD = 0,
  AUTONOMOUS = 1,
  AUTONOMOUS_WITH_REMOTE_SETPOINT = 2,
};

enum class UsbTimeoutMode : uint8_t {
  ENFORCE = 0,
  DISABLE = 1,
};

} // namespace gateway
