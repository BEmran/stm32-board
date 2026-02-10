
#pragma once
#include "core/basic.hpp"

#include <cstdint>
#include <cstddef>

namespace connection {

// Fixed binary packet published by the gateway (Pi -> clients).
// Wire format: all multi-byte fields are big-endian (network order).
// Keep this stable: add new fields at the end and bump version/flags if needed.
#pragma pack(push, 1)
struct StatePktV1 {
  uint32_t seq;      // incrementing sequence number
  float   t_mono_s; // steady clock seconds since start (monotonic)

  // IMU raw (already scaled by Rosmaster parsing)
  float ax, ay, az;
  float gx, gy, gz;
  float mx, my, mz;

  // attitude (as provided by board / library)
  float roll, pitch, yaw;

  // encoders
  int32_t e1, e2, e3, e4;

  float battery_voltage;
};
#pragma pack(pop)

static_assert(sizeof(StatePktV1) == 76, "StatePktV1 must be 76 bytes");

#pragma pack(push, 1)
struct CmdPktV1 {
  uint32_t seq;            // incrementing sequence number from controller
  int16_t m1, m2, m3, m4;  // motor command -100..100 (match Rosmaster set_motor)
  uint16_t beep_ms;        // 0 = no beep, otherwise duration
  uint8_t flags;           // reserved for future
};
#pragma pack(pop)

static_assert(sizeof(CmdPktV1) == 15, "CmdPktV1 must be 15 bytes");

StatePktV1 state_to_state_pktv1(uint32_t seq, float t_mono_s, core::State state);
core::Actions cmd_pktv1_to_actions(CmdPktV1 pkt);
CmdPktV1 cmd_pktv1_host_to_net(CmdPktV1 host);
CmdPktV1 cmd_pktv1_net_to_host(CmdPktV1 net);
StatePktV1 state_pktv1_host_to_net(StatePktV1 host);
StatePktV1 state_pktv1_net_to_host(StatePktV1 net);
} // namespace connection
