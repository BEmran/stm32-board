#pragma once
#include <cstdint>

namespace rosmaster {

// Matches Python constants
inline constexpr uint8_t HEAD      = 0xFF;
inline constexpr uint8_t DEVICE_ID = 0xFC;
inline constexpr uint8_t COMPLEMENT = static_cast<uint8_t>(257 - DEVICE_ID); // used in checksum sum(..., COMPLEMENT)

enum Func : uint8_t {
  FUNC_AUTO_REPORT     = 0x01,
  FUNC_BEEP            = 0x02,

  FUNC_REPORT_SPEED    = 0x0A,
  FUNC_REPORT_MPU_RAW  = 0x0B,
  FUNC_REPORT_IMU_ATT  = 0x0C,
  FUNC_REPORT_ENCODER  = 0x0D,
  FUNC_REPORT_ICM_RAW  = 0x0E,

  FUNC_RESET_STATE     = 0x0F,

  FUNC_MOTOR           = 0x10,
  FUNC_CAR_RUN         = 0x11,
  FUNC_MOTION          = 0x12,

  FUNC_REQUEST_DATA    = 0x50,
  FUNC_VERSION         = 0x51,
};

// helpers: little-endian decode
inline int16_t le_i16(const uint8_t* p) {
  return static_cast<int16_t>(p[0] | (p[1] << 8));
}

inline int32_t le_i32(const uint8_t* p) {
  return static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

} // namespace rosmaster
