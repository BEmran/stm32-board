
#pragma once
#include "core/basic.hpp"

#include <cstdint>
#include <cstddef>

namespace connection
{

// Fixed binary packet published by the gateway (Pi -> clients).
// Wire format: all multi-byte fields are little-endian (LE).
// Keep this stable: add new fields at the end and bump version/flags if needed.
#pragma pack(push, 1)
  struct StatesPkt
  {
    uint32_t seq{0};     // incrementing sequence number
    float t_mono_s{0.0}; // steady clock seconds since start (monotonic)
    core::States state;
  };
#pragma pack(pop)

  static_assert(sizeof(StatesPkt) == 76, "StatesPkt must be 76 bytes");

#pragma pack(push, 1)
  struct CmdPkt
  {
    uint32_t seq; // incrementing sequence number from controller
    core::Actions actions;
  };
#pragma pack(pop)

  static_assert(sizeof(CmdPkt) == 14, "CmdPkt must be 14 bytes");

   StatesPkt state_to_state_pkt(uint32_t seq, float t_mono_s, core::States state);
} // namespace connection
