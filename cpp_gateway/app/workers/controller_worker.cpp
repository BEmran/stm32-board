#include "controller_worker.hpp"

#include "utils/logger.hpp"

#include <chrono>
#include <thread>

namespace workers {

static inline void sleep_to_rate(double hz, std::chrono::steady_clock::time_point& next_tp) {
  using clock = std::chrono::steady_clock;
  if (hz <= 0.0) hz = 1.0;
  const auto dt = std::chrono::duration<double>(1.0 / hz);
  next_tp += std::chrono::duration_cast<clock::duration>(dt);
  std::this_thread::sleep_until(next_tp);
}

static inline bool bit_matches(int bit, uint8_t idx) {
  return (bit >= 0 && bit < 8 && static_cast<uint8_t>(bit) == idx);
}

ControllerWorker::ControllerWorker(SharedState& sh, gateway::StopFlag& stop)
  : sh_(sh), stop_(stop) {}

void ControllerWorker::operator()() {
  using clock = std::chrono::steady_clock;
  auto next_tp = clock::now();

  bool warned_timeout = false;

  while (!stop_.stop_requested()) {
    auto cfg_ptr = sh_.cfg.load(std::memory_order_acquire);
    const double ctrl_hz = cfg_ptr ? cfg_ptr->ctrl_hz : 200.0;

    // Read snapshots
    const core::States st = sh_.latest_state.load_or_default();
    const core::Actions remote_cmd = sh_.latest_remote_cmd.load_or_default();
    const connection::SetpointPkt sp = sh_.latest_setpoint_cmd.load_or_default();

    auto sys = sh_.system_state.load_or_default();
    if (cfg_ptr) sys.control_mode = cfg_ptr->control_mode;

    // Drain sys events (bounded)
    sh_.sys_event_q.drain(32, [&](const gateway::EventCmd& ev) {
      if (ev.type != gateway::EventType::FLAG_RISE) return;

      const uint8_t bit = ev.data0;

      // Start/Stop/Reset mapping (optional)
      if (cfg_ptr && bit_matches(cfg_ptr->flag_start_bit, bit)) {
        sys.running = true;
      }
      if (cfg_ptr && bit_matches(cfg_ptr->flag_stop_bit, bit)) {
        sys.running = false;
      }
      if (cfg_ptr && bit_matches(cfg_ptr->flag_reset_bit, bit)) {
        // conservative reset behavior: stop + clear continuous commands
        sys.running = false;

        core::Actions zero{};
        zero.beep_ms = 0;
        zero.flags = 0;
        sh_.latest_remote_cmd.store(zero);

        connection::SetpointPkt zsp{};
        sh_.latest_setpoint_cmd.store(zsp);
      }
    });

    // Cmd timeout enforcement
    bool cmd_timeout_active = false;
    if (cfg_ptr && cfg_ptr->usb_timeout_mode == gateway::UsbTimeoutMode::ENFORCE) {
      const double now_mono = now_timestamps().mono_s;
      const double last_rx = sh_.last_cmd_rx_mono_s.load(std::memory_order_acquire);
      if (last_rx > 0.0 && (now_mono - last_rx) > cfg_ptr->cmd_timeout_s) {
        cmd_timeout_active = true;
        if (!warned_timeout) {
          warned_timeout = true;
          logger::warn() << "[CTRL] CMD timeout: " << (now_mono - last_rx)
                         << "s > " << cfg_ptr->cmd_timeout_s << "s. Forcing motors=0.\n";
        }
      } else {
        warned_timeout = false;
      }
    }

    // Compute desired action
    core::Actions out{};
    out.beep_ms = 0;                 // never continuous
    out.flags   = sys.continuous_flags;

    if (!sys.running || cmd_timeout_active) {
      out.motors = {};
    } else {
      switch (sys.control_mode) {
        case gateway::ControlMode::PASS_THROUGH_CMD:
          out = remote_cmd;
          out.beep_ms = 0;
          out.flags = sys.continuous_flags;
          break;

        case gateway::ControlMode::AUTONOMOUS:
          (void)st;
          out.motors = {}; // placeholder OK
          break;

        case gateway::ControlMode::AUTONOMOUS_WITH_REMOTE_SETPOINT:
          // Placeholder controller – preserves dataflow: st + sp available here.
          (void)st;
          (void)sp;
          out.motors = {}; // placeholder OK
          break;
      }
    }

    sh_.system_state.store(sys);
    sh_.latest_action_request.store(out);

    sleep_to_rate(ctrl_hz, next_tp);
  }

  logger::info() << "[CTRL] Stopped.\n";
}

} // namespace workers
