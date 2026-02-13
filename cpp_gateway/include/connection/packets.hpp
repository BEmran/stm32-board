#pragma once
#include "core/basic.hpp"

#include <cstdint>
#include <cstddef>

namespace connection
{

// Fixed binary packet published by the gateway (Pi -> clients).
// Wire format: all multi-byte fields are little-endian (LE).
#pragma pack(push, 1)
  struct StatesPkt
  {
    uint32_t seq{0};     // incrementing sequence number
    float t_mono_s{0.0}; // steady clock seconds since start (monotonic)
    core::States state;
  };
#pragma pack(pop)

  static_assert(sizeof(StatesPkt) == 76, "StatesPkt must be 76 bytes");

// Legacy CMD packet (MUST NOT CHANGE)
#pragma pack(push, 1)
  struct CmdPkt
  {
    uint32_t seq; // incrementing sequence number from controller
    core::Actions actions;
  };
#pragma pack(pop)

  static_assert(sizeof(CmdPkt) == 14, "CmdPkt must be 14 bytes");

// New: SetpointPkt (21 bytes)
#pragma pack(push, 1)
  struct SetpointPkt
  {
    uint32_t seq;
    float sp0{0.0f};
    float sp1{0.0f};
    float sp2{0.0f};
    float sp3{0.0f};
    uint8_t flags{0};
  };
#pragma pack(pop)

  static_assert(sizeof(SetpointPkt) == 21, "SetpointPkt must be 21 bytes");

// New: ConfigPkt (12 bytes)
#pragma pack(push, 1)
  struct ConfigPkt
  {
    uint32_t seq;
    uint8_t  key{0};
    uint8_t  u8{0};
    uint16_t u16{0};
    uint32_t u32{0};
  };
#pragma pack(pop)

  static_assert(sizeof(ConfigPkt) == 12, "ConfigPkt must be 12 bytes");

  StatesPkt state_to_state_pkt(uint32_t seq, float t_mono_s, core::States state);

} // namespace connection
