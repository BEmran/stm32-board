#pragma once
#include "core/basic.hpp"

#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace connection::wire {

/**
 * @brief Wire format utilities for the gateway TCP protocol.
 *
 * IMPORTANT:
 * - The wire format is explicitly defined as LITTLE-ENDIAN for all multi-byte fields.
 * - Floats are IEEE-754 binary32, transmitted as their raw uint32 bit pattern (little-endian).
 *
 * This makes the protocol decodable by external clients (Python/Matlab/etc.) without relying
 * on C/C++ struct packing/alignment.
 */

// ---- Fixed payload sizes (bytes) ----
inline constexpr size_t kStatesPayloadSize   = 76; // seq(u32) + t_mono_s(f32) + core::States (explicit field order)
inline constexpr size_t kMotorCmdPayloadSize      = 12; // seq(u32) + motors(4*i16)
inline constexpr size_t kSetpointPayloadSize = 21; // seq(u32) + sp0..sp3(f32) + flags(u8)
inline constexpr size_t kConfigPayloadSize   = 12; // seq(u32) + key(u8) + u8 + u16 + u32

// Stats response (MSG_STATS_RESP). Keep small and fixed.
inline constexpr size_t kStatsPayloadSize = 48; // versioned fixed struct, see encode/decode

// ---- Endian helpers ----
inline void write_u16_le(uint8_t* out, uint16_t v) noexcept {
  out[0] = static_cast<uint8_t>(v & 0xFF);
  out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

inline void write_u32_le(uint8_t* out, uint32_t v) noexcept {
  out[0] = static_cast<uint8_t>(v & 0xFF);
  out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

inline uint16_t read_u16_le(const uint8_t* in) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(in[0]) |
                               (static_cast<uint16_t>(in[1]) << 8));
}

inline uint32_t read_u32_le(const uint8_t* in) noexcept {
  return static_cast<uint32_t>(static_cast<uint32_t>(in[0]) |
                               (static_cast<uint32_t>(in[1]) << 8) |
                               (static_cast<uint32_t>(in[2]) << 16) |
                               (static_cast<uint32_t>(in[3]) << 24));
}

inline void write_i16_le(uint8_t* out, int16_t v) noexcept {
  write_u16_le(out, static_cast<uint16_t>(v));
}

inline int16_t read_i16_le(const uint8_t* in) noexcept {
  return static_cast<int16_t>(read_u16_le(in));
}

inline void write_i32_le(uint8_t* out, int32_t v) noexcept {
  write_u32_le(out, static_cast<uint32_t>(v));
}

inline int32_t read_i32_le(const uint8_t* in) noexcept {
  return static_cast<int32_t>(read_u32_le(in));
}

inline void write_f32_le(uint8_t* out, float f) noexcept {
  const uint32_t bits = std::bit_cast<uint32_t>(f);
  write_u32_le(out, bits);
}

inline float read_f32_le(const uint8_t* in) noexcept {
  const uint32_t bits = read_u32_le(in);
  return std::bit_cast<float>(bits);
}

// ---- Logical payload structs (independent of layout/padding) ----
struct MotorCmdPayload {
  uint32_t seq{0};
  core::MotorCommands motors{};
};

struct SetpointPayload {
  uint32_t seq{0};
  float sp[4]{0,0,0,0};
  uint8_t flags{0};
};

struct ConfigPayload {
  uint32_t seq{0};
  uint8_t  key{0};
  uint8_t  u8{0};
  uint16_t u16{0};
  uint32_t u32{0};
};

struct StatsPayload {
  uint32_t seq{0};
  uint32_t uptime_ms{0};
  float    usb_hz{0.f};
  float    tcp_hz{0.f};
  float    ctrl_hz{0.f};
  uint32_t drops_state{0};
  uint32_t drops_cmd{0};
  uint32_t drops_event{0};
  uint32_t drops_sys_event{0};
  uint32_t tcp_frames_bad{0};
  uint32_t serial_errors{0};
  uint32_t reserved0{0};
};

// ---- Encoders (return false if span has wrong size) ----
bool encode_states_payload(std::span<uint8_t> out, uint32_t seq, float t_mono_s, const core::States& st) noexcept;

bool encode_cmd_payload(std::span<uint8_t> out, const MotorCmdPayload& p) noexcept;
bool decode_cmd_payload(std::span<const uint8_t> in, MotorCmdPayload& out) noexcept;

bool encode_setpoint_payload(std::span<uint8_t> out, const SetpointPayload& p) noexcept;
bool decode_setpoint_payload(std::span<const uint8_t> in, SetpointPayload& out) noexcept;

bool encode_config_payload(std::span<uint8_t> out, const ConfigPayload& p) noexcept;
bool decode_config_payload(std::span<const uint8_t> in, ConfigPayload& out) noexcept;

bool encode_stats_payload(std::span<uint8_t> out, const StatsPayload& p) noexcept;
bool decode_stats_payload(std::span<const uint8_t> in, StatsPayload& out) noexcept;

} // namespace connection::wire
