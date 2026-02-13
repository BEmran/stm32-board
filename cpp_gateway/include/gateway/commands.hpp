#pragma once
#include <cstdint>

namespace gateway {

enum class EventType : uint8_t {
  BEEP = 0,
  FLAG_RISE = 1,
  CONFIG_APPLIED = 2,
};

#pragma pack(push, 1)
struct EventCmd {
  EventType type{EventType::BEEP};
  uint32_t  seq{0};
  uint8_t   data0{0};
  uint8_t   data1{0};
  uint8_t   data2{0};
  uint8_t   data3{0};
  uint32_t  aux_u32{0};
};
#pragma pack(pop)

static_assert(sizeof(EventCmd) == 13, "EventCmd must be 13 bytes");

} // namespace gateway
