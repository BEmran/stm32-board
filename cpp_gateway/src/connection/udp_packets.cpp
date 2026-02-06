
#include "connection/udp_packets.hpp"

namespace connection
{

  StatePktV1 state_to_state_pktv1(uint32_t seq, double t_mono_s, core::State state)
  {
    return StatePktV1{
        .seq = seq,
        .t_mono_s = t_mono_s,
        .ax = state.imu.acc.x,
        .ay = state.imu.acc.y,
        .az = state.imu.acc.z,
        .gx = state.imu.gyro.x,
        .gy = state.imu.gyro.y,
        .gz = state.imu.gyro.z,
        .mx = state.imu.mag.x,
        .my = state.imu.mag.y,
        .mz = state.imu.mag.z,
        .roll = state.ang.roll,
        .pitch = state.ang.pitch,
        .yaw = state.ang.yaw,
        .e1 = state.enc.e1,
        .e2 = state.enc.e2,
        .e3 = state.enc.e3,
        .e4 = state.enc.e4,
        .battery_voltage = state.battery_voltage
      };
  }

  core::Actions cmd_pktv1_to_actions(CmdPktV1 pkt)
  {
    return core::Actions{
        .motors = core::MotorCommands{
          .m1 = pkt.m1,
          .m2 = pkt.m2,
          .m3 = pkt.m3,
          .m4 = pkt.m4,
        },
        .beep_ms = pkt.beep_ms,
        .flags = pkt.flags
      };
  }
} // namespace connection
