#pragma once
#include <cstdint>
#include <cstddef>

// Must match Python protocol.py exactly:
// STATE_STRUCT = "<Idffffffffffffiiii"  -> 72 bytes
// CMD_STRUCT   = "<IHHHHHH"            -> 16 bytes
//
// Little-endian assumed (true on Raspberry Pi). If you later run on a different
// endianness, add explicit LE conversions in serialization.

#pragma pack(push, 1)

struct StatePkt {
  uint32_t seq;     // I
  double   t_mono;  // d

  float ax, ay, az; // fff
  float gx, gy, gz; // fff
  float mx, my, mz; // fff

  float roll, pitch, yaw; // fff

  int32_t e1, e2, e3, e4; // iiii
};

struct CmdPkt {
  uint32_t seq;           // I
  uint16_t m1, m2, m3, m4;// HHHH
  uint16_t beep_ms;       // H
  uint16_t flags;         // H
};


#pragma pack(pop)

void print_state(const StatePkt &s);

static_assert(sizeof(StatePkt) == 76, "StatePkt size must be 72 bytes");
static_assert(sizeof(CmdPkt)   == 16, "CmdPkt size must be 16 bytes");
