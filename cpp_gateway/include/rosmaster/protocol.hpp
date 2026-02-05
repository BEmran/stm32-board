#pragma once
#include <cstdint>

namespace rosmaster {

inline constexpr uint8_t HEAD      = 0xFF;
inline constexpr uint8_t DEVICE_ID = 0xFC;
inline constexpr uint8_t COMPLEMENT = static_cast<uint8_t>(257 - DEVICE_ID);

// Function words (matches Rosmaster_Lib.py)
enum Func : uint8_t {
  FUNC_AUTO_REPORT      = 0x01,
  FUNC_BEEP             = 0x02,
  FUNC_PWM_SERVO        = 0x03,
  FUNC_PWM_SERVO_ALL    = 0x04,
  FUNC_RGB              = 0x05,
  FUNC_RGB_EFFECT       = 0x06,

  FUNC_REPORT_SPEED     = 0x0A,
  FUNC_REPORT_MPU_RAW   = 0x0B,
  FUNC_REPORT_IMU_ATT   = 0x0C,
  FUNC_REPORT_ENCODER   = 0x0D,
  FUNC_REPORT_ICM_RAW   = 0x0E,

  FUNC_RESET_STATE      = 0x0F,

  FUNC_MOTOR            = 0x10,
  FUNC_CAR_RUN          = 0x11,
  FUNC_MOTION           = 0x12,
  FUNC_SET_MOTOR_PID    = 0x13,
  FUNC_SET_YAW_PID      = 0x14, // present in python (commented setters/getters)
  FUNC_SET_CAR_TYPE     = 0x15,

  FUNC_UART_SERVO       = 0x20,
  FUNC_UART_SERVO_ID    = 0x21,
  FUNC_UART_SERVO_TORQUE= 0x22,
  FUNC_ARM_CTRL         = 0x23,
  FUNC_ARM_OFFSET       = 0x24,

  FUNC_AKM_DEF_ANGLE    = 0x30,
  FUNC_AKM_STEER_ANGLE  = 0x31,

  FUNC_REQUEST_DATA     = 0x50,
  FUNC_VERSION          = 0x51,

  FUNC_RESET_FLASH      = 0xA0,
};

inline int16_t le_i16(const uint8_t* p) {
  return static_cast<int16_t>(p[0] | (p[1] << 8));
}
inline int32_t le_i32(const uint8_t* p) {
  return static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

} // namespace rosmaster
