#pragma once
#include "connection/wire_codec.hpp"

#include <array>
#include <cstdint>

namespace connection {

/**
 * @brief Backwards-compatibility header.
 *
 * The gateway wire format is defined in @ref connection::wire (see wire_codec.hpp).
 * External clients (Python/Matlab/etc.) SHOULD follow the explicit little-endian layout
 * described in docs/protocol.md instead of relying on C/C++ struct packing.
 *
 * This header remains to keep include paths stable.
 */

using StatesPayloadBytes   = std::array<uint8_t, wire::kStatesPayloadSize>;
using CmdPayloadBytes      = std::array<uint8_t, wire::kCmdPayloadSize>;
using SetpointPayloadBytes = std::array<uint8_t, wire::kSetpointPayloadSize>;
using ConfigPayloadBytes   = std::array<uint8_t, wire::kConfigPayloadSize>;
using StatsPayloadBytes    = std::array<uint8_t, wire::kStatsPayloadSize>;

inline StatesPayloadBytes make_states_payload(uint32_t seq, float t_mono_s, const core::States& st) {
  StatesPayloadBytes b{};
  (void)wire::encode_states_payload(b, seq, t_mono_s, st);
  return b;
}

} // namespace connection
