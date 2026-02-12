#pragma once

#include <cstdint>

namespace utils {
[[nodiscard]] constexpr int16_t le_i16(const uint8_t* p) noexcept {
  return static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                              (static_cast<uint16_t>(p[1]) << 8));
}
[[nodiscard]] constexpr int32_t le_i32(const uint8_t* p) noexcept {
  return static_cast<int32_t>(static_cast<uint32_t>(p[0]) |
                              (static_cast<uint32_t>(p[1]) << 8) |
                              (static_cast<uint32_t>(p[2]) << 16) |
                              (static_cast<uint32_t>(p[3]) << 24));
}
} // namespace utils
