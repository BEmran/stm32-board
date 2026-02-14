#pragma once
#include "connection/packets.hpp"
#include "core/basic.hpp"
#include "gateway/latest_value.hpp"
#include "gateway/runtime_config.hpp"
#include "gateway/spsc_overwrite_ring.hpp"
#include "gateway/commands.hpp"
#include "gateway/enums.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace workers {

inline core::Timestamps now_timestamps() noexcept {
  using sys = std::chrono::system_clock;
  using st  = std::chrono::steady_clock;

  core::Timestamps ts{};
  ts.epoch_s = std::chrono::duration<double>(sys::now().time_since_epoch()).count();
  ts.mono_s  = std::chrono::duration<double>(st::now().time_since_epoch()).count();
  return ts;
}

struct SystemState {
  bool running{false};
  gateway::ControlMode control_mode{gateway::ControlMode::PASS_THROUGH_CMD};
  uint8_t continuous_flags{0};
};

#pragma pack(push, 1)
struct StateSample {
  core::Timestamps ts{};
  uint32_t seq{0};
  core::States st{};
};

struct ActionSample {
  core::Timestamps ts{};
  uint32_t seq{0};
  core::Actions act{};
};

struct EventSample {
  core::Timestamps ts{};
  gateway::EventCmd ev{};
};
#pragma pack(pop)

struct SharedState {
  std::atomic<gateway::RuntimeConfigPtr> cfg;

  gateway::LatestValue<core::States> latest_state;

  // Continuous / latest-wins commands
  gateway::LatestValue<core::Actions>        latest_remote_cmd;     // legacy cmd (beep cleared, event bits removed)
  gateway::LatestValue<connection::wire::SetpointPayload> latest_setpoint_cmd; // setpoint (latest-wins)
  gateway::LatestValue<core::Actions>        latest_action_request;  // controller -> USB
  gateway::LatestValue<SystemState>          system_state;

  // Safety: last time we received any "command" from TCP side (mono seconds)
  std::atomic<double> last_cmd_rx_mono_s{0.0};

  // Diagnostics counters
  std::atomic<uint32_t> tcp_frames_bad{0};
  std::atomic<uint32_t> serial_errors{0};
  const double start_mono_s{now_timestamps().mono_s};

  // One-shot event queues (overwrite-on-full)
  gateway::SpscOverwriteRing<gateway::EventCmd, 256> event_cmd_q; // TCP -> USB (HW events)
  gateway::SpscOverwriteRing<gateway::EventCmd, 256> sys_event_q; // TCP -> Controller (sys events)

  // Logger rings
  gateway::SpscOverwriteRing<StateSample, 4096>  state_ring;
  gateway::SpscOverwriteRing<ActionSample, 2048> action_ring;
  gateway::SpscOverwriteRing<EventSample, 2048>  event_ring;
  gateway::SpscOverwriteRing<EventSample, 2048>  sys_event_ring;
};

} // namespace workers
