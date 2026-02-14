#include "connection/wire_codec.hpp"

#include <cassert>
#include <array>

int main() {
  // Cmd encode/decode roundtrip
  connection::wire::CmdPayload c{};
  c.seq = 42;
  c.actions.motors.m1 = -10;
  c.actions.motors.m2 = 20;
  c.actions.motors.m3 = 30;
  c.actions.motors.m4 = 40;
  c.actions.beep_ms = 7;
  c.actions.flags = 0xA5;

  std::array<uint8_t, connection::wire::kCmdPayloadSize> buf{};
  assert(connection::wire::encode_cmd_payload(buf, c));

  connection::wire::CmdPayload out{};
  assert(connection::wire::decode_cmd_payload(buf, out));
  assert(out.seq == c.seq);
  assert(out.actions.motors.m1 == c.actions.motors.m1);
  assert(out.actions.flags == c.actions.flags);

  // States encode size check
  core::States st{};
  st.imu.acc.x = 1.25f;
  std::array<uint8_t, connection::wire::kStatesPayloadSize> sbuf{};
  assert(connection::wire::encode_states_payload(sbuf, 1, 0.5f, st));

  return 0;
}
