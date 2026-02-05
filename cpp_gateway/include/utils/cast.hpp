#pragma once

#include <cstdint>

namespace utils {
inline int16_t le_i16(const uint8_t* p) {
  return static_cast<int16_t>(p[0] | (p[1] << 8));
}
inline int32_t le_i32(const uint8_t* p) {
  return static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}
}