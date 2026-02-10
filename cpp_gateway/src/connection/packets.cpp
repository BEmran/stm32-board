
#include "connection/packets.hpp"

#include <arpa/inet.h>
#include <cstring>

namespace connection
{
  namespace {
    uint32_t float_to_u32(float v) {
      uint32_t u = 0;
      std::memcpy(&u, &v, sizeof(u));
      return u;
    }

    float u32_to_float(uint32_t u) {
      float v = 0.0f;
      std::memcpy(&v, &u, sizeof(v));
      return v;
    }

    void store_be_f32(float v, float& out_field) {
      const uint32_t be = htonl(float_to_u32(v));
      std::memcpy(&out_field, &be, sizeof(be));
    }

    float load_be_f32(const float& in_field) {
      uint32_t be = 0;
      std::memcpy(&be, &in_field, sizeof(be));
      return u32_to_float(ntohl(be));
    }
  } // namespace

  StatePktV1 state_to_state_pktv1(uint32_t seq, float t_mono_s, core::State state)
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

  CmdPktV1 cmd_pktv1_host_to_net(CmdPktV1 host)
  {
    CmdPktV1 net{};
    net.seq = htonl(host.seq);
    net.m1 = static_cast<int16_t>(htons(static_cast<uint16_t>(host.m1)));
    net.m2 = static_cast<int16_t>(htons(static_cast<uint16_t>(host.m2)));
    net.m3 = static_cast<int16_t>(htons(static_cast<uint16_t>(host.m3)));
    net.m4 = static_cast<int16_t>(htons(static_cast<uint16_t>(host.m4)));
    net.beep_ms = htons(host.beep_ms);
    net.flags = host.flags;
    return net;
  }

  CmdPktV1 cmd_pktv1_net_to_host(CmdPktV1 net)
  {
    CmdPktV1 host{};
    host.seq = ntohl(net.seq);
    host.m1 = static_cast<int16_t>(ntohs(static_cast<uint16_t>(net.m1)));
    host.m2 = static_cast<int16_t>(ntohs(static_cast<uint16_t>(net.m2)));
    host.m3 = static_cast<int16_t>(ntohs(static_cast<uint16_t>(net.m3)));
    host.m4 = static_cast<int16_t>(ntohs(static_cast<uint16_t>(net.m4)));
    host.beep_ms = ntohs(net.beep_ms);
    host.flags = net.flags;
    return host;
  }

  StatePktV1 state_pktv1_host_to_net(StatePktV1 host)
  {
    StatePktV1 net{};
    net.seq = htonl(host.seq);
    store_be_f32(host.t_mono_s, net.t_mono_s);
    store_be_f32(host.ax, net.ax);
    store_be_f32(host.ay, net.ay);
    store_be_f32(host.az, net.az);
    store_be_f32(host.gx, net.gx);
    store_be_f32(host.gy, net.gy);
    store_be_f32(host.gz, net.gz);
    store_be_f32(host.mx, net.mx);
    store_be_f32(host.my, net.my);
    store_be_f32(host.mz, net.mz);
    store_be_f32(host.roll, net.roll);
    store_be_f32(host.pitch, net.pitch);
    store_be_f32(host.yaw, net.yaw);
    net.e1 = static_cast<int32_t>(htonl(static_cast<uint32_t>(host.e1)));
    net.e2 = static_cast<int32_t>(htonl(static_cast<uint32_t>(host.e2)));
    net.e3 = static_cast<int32_t>(htonl(static_cast<uint32_t>(host.e3)));
    net.e4 = static_cast<int32_t>(htonl(static_cast<uint32_t>(host.e4)));
    store_be_f32(host.battery_voltage, net.battery_voltage);
    return net;
  }

  StatePktV1 state_pktv1_net_to_host(StatePktV1 net)
  {
    StatePktV1 host{};
    host.seq = ntohl(net.seq);
    host.t_mono_s = load_be_f32(net.t_mono_s);
    host.ax = load_be_f32(net.ax);
    host.ay = load_be_f32(net.ay);
    host.az = load_be_f32(net.az);
    host.gx = load_be_f32(net.gx);
    host.gy = load_be_f32(net.gy);
    host.gz = load_be_f32(net.gz);
    host.mx = load_be_f32(net.mx);
    host.my = load_be_f32(net.my);
    host.mz = load_be_f32(net.mz);
    host.roll = load_be_f32(net.roll);
    host.pitch = load_be_f32(net.pitch);
    host.yaw = load_be_f32(net.yaw);
    host.e1 = static_cast<int32_t>(ntohl(static_cast<uint32_t>(net.e1)));
    host.e2 = static_cast<int32_t>(ntohl(static_cast<uint32_t>(net.e2)));
    host.e3 = static_cast<int32_t>(ntohl(static_cast<uint32_t>(net.e3)));
    host.e4 = static_cast<int32_t>(ntohl(static_cast<uint32_t>(net.e4)));
    host.battery_voltage = load_be_f32(net.battery_voltage);
    return host;
  }
} // namespace connection
