#include "connection/wire_codec.hpp"

#include <cassert>
#include <array>

int main() {
  // Cmd encode/decode roundtrip
  connection::wire::MotorCmdPayload cmd{};
  cmd.seq = 42;
  cmd.motors.m1 = -10;
  cmd.motors.m2 = 20;
  cmd.motors.m3 = 30;
  cmd.motors.m4 = 40;

  std::array<uint8_t, connection::wire::kMotorCmdPayloadSize> buf{};
  assert(connection::wire::encode_cmd_payload(buf, cmd));

  connection::wire::MotorCmdPayload out{};
  assert(connection::wire::decode_cmd_payload(buf, out));
  assert(out.seq == cmd.seq);
  assert(out.motors.m1 == cmd.motors.m1);

  // States encode size check
  core::States st{};
  st.imu.acc.x = 1.25f;
  std::array<uint8_t, connection::wire::kStatesPayloadSize> sbuf{};
  assert(connection::wire::encode_states_payload(sbuf, 1, 0.5f, st));

  return 0;
}
