#include "connection/wire_codec.hpp"

namespace connection::wire {

bool encode_states_payload(std::span<uint8_t> out, uint32_t seq, float t_mono_s, const core::States& st) noexcept {
  if (out.size() != kStatesPayloadSize) return false;
  size_t o = 0;

  write_u32_le(out.data() + o, seq); o += 4;
  write_f32_le(out.data() + o, t_mono_s); o += 4;

  // IMU acc, gyro, mag (Vec3d each)
  const auto write_vec3 = [&](const core::Vec3d& v) {
    write_f32_le(out.data()+o, v.x); o+=4;
    write_f32_le(out.data()+o, v.y); o+=4;
    write_f32_le(out.data()+o, v.z); o+=4;
  };
  write_vec3(st.imu.acc);
  write_vec3(st.imu.gyro);
  write_vec3(st.imu.mag);

  // Angles
  write_f32_le(out.data()+o, st.ang.roll);  o+=4;
  write_f32_le(out.data()+o, st.ang.pitch); o+=4;
  write_f32_le(out.data()+o, st.ang.yaw);   o+=4;

  // Encoders (i32)
  write_i32_le(out.data()+o, st.enc.e1); o+=4;
  write_i32_le(out.data()+o, st.enc.e2); o+=4;
  write_i32_le(out.data()+o, st.enc.e3); o+=4;
  write_i32_le(out.data()+o, st.enc.e4); o+=4;

  // Battery
  write_f32_le(out.data()+o, st.battery_voltage); o+=4;

  return (o == kStatesPayloadSize);
}

bool encode_cmd_payload(std::span<uint8_t> out, const MotorCmdPayload& p) noexcept {
  if (out.size() != kMotorCmdPayloadSize) return false;
  size_t o = 0;
  write_u32_le(out.data()+o, p.seq); o+=4;
  write_i16_le(out.data()+o, p.motors.m1); o+=2;
  write_i16_le(out.data()+o, p.motors.m2); o+=2;
  write_i16_le(out.data()+o, p.motors.m3); o+=2;
  write_i16_le(out.data()+o, p.motors.m4); o+=2;
  return (o == kMotorCmdPayloadSize);
}

bool decode_cmd_payload(std::span<const uint8_t> in, MotorCmdPayload& out) noexcept {
  if (in.size() != kMotorCmdPayloadSize) return false;
  size_t o = 0;
  out.seq = read_u32_le(in.data()+o); o+=4;
  out.motors.m1 = read_i16_le(in.data()+o); o+=2;
  out.motors.m2 = read_i16_le(in.data()+o); o+=2;
  out.motors.m3 = read_i16_le(in.data()+o); o+=2;
  out.motors.m4 = read_i16_le(in.data()+o); o+=2;
  return (o == kMotorCmdPayloadSize);
}

bool encode_setpoint_payload(std::span<uint8_t> out, const SetpointPayload& p) noexcept {
  if (out.size() != kSetpointPayloadSize) return false;
  size_t o = 0;
  write_u32_le(out.data()+o, p.seq); o+=4;
  for (int i=0;i<4;i++){ write_f32_le(out.data()+o, p.sp[i]); o+=4; }
  out[o++] = p.flags;
  return (o == kSetpointPayloadSize);
}

bool decode_setpoint_payload(std::span<const uint8_t> in, SetpointPayload& outp) noexcept {
  if (in.size() != kSetpointPayloadSize) return false;
  size_t o = 0;
  outp.seq = read_u32_le(in.data()+o); o+=4;
  for(int i=0;i<4;i++){ outp.sp[i]=read_f32_le(in.data()+o); o+=4; }
  outp.flags = in[o++];
  return (o == kSetpointPayloadSize);
}

bool encode_config_payload(std::span<uint8_t> out, const ConfigPayload& p) noexcept {
  if (out.size() != kConfigPayloadSize) return false;
  size_t o = 0;
  write_u32_le(out.data()+o, p.seq); o+=4;
  out[o++] = p.key;
  out[o++] = p.u8;
  write_u16_le(out.data()+o, p.u16); o+=2;
  write_u32_le(out.data()+o, p.u32); o+=4;
  return (o == kConfigPayloadSize);
}

bool decode_config_payload(std::span<const uint8_t> in, ConfigPayload& outp) noexcept {
  if (in.size() != kConfigPayloadSize) return false;
  size_t o = 0;
  outp.seq = read_u32_le(in.data()+o); o+=4;
  outp.key = in[o++];
  outp.u8  = in[o++];
  outp.u16 = read_u16_le(in.data()+o); o+=2;
  outp.u32 = read_u32_le(in.data()+o); o+=4;
  return (o == kConfigPayloadSize);
}

bool encode_stats_payload(std::span<uint8_t> out, const StatsPayload& p) noexcept {
  if (out.size() != kStatsPayloadSize) return false;
  size_t o=0;
  write_u32_le(out.data()+o, p.seq); o+=4;
  write_u32_le(out.data()+o, p.uptime_ms); o+=4;
  write_f32_le(out.data()+o, p.usb_hz); o+=4;
  write_f32_le(out.data()+o, p.tcp_hz); o+=4;
  write_f32_le(out.data()+o, p.ctrl_hz); o+=4;
  write_u32_le(out.data()+o, p.drops_state); o+=4;
  write_u32_le(out.data()+o, p.drops_cmd); o+=4;
  write_u32_le(out.data()+o, p.drops_event); o+=4;
  write_u32_le(out.data()+o, p.drops_sys_event); o+=4;
  write_u32_le(out.data()+o, p.tcp_frames_bad); o+=4;
  write_u32_le(out.data()+o, p.serial_errors); o+=4;
  write_u32_le(out.data()+o, p.reserved0); o+=4;
  // pad to fixed size
  while(o<kStatsPayloadSize){ out[o++]=0; }
  return true;
}

bool decode_stats_payload(std::span<const uint8_t> in, StatsPayload& p) noexcept {
  if (in.size() != kStatsPayloadSize) return false;
  size_t o=0;
  p.seq = read_u32_le(in.data()+o); o+=4;
  p.uptime_ms = read_u32_le(in.data()+o); o+=4;
  p.usb_hz = read_f32_le(in.data()+o); o+=4;
  p.tcp_hz = read_f32_le(in.data()+o); o+=4;
  p.ctrl_hz = read_f32_le(in.data()+o); o+=4;
  p.drops_state = read_u32_le(in.data()+o); o+=4;
  p.drops_cmd = read_u32_le(in.data()+o); o+=4;
  p.drops_event = read_u32_le(in.data()+o); o+=4;
  p.drops_sys_event = read_u32_le(in.data()+o); o+=4;
  p.tcp_frames_bad = read_u32_le(in.data()+o); o+=4;
  p.serial_errors = read_u32_le(in.data()+o); o+=4;
  p.reserved0 = read_u32_le(in.data()+o); o+=4;
  return true;
}

} // namespace connection::wire
