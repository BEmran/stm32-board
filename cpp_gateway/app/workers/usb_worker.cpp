#include "usb_worker.hpp"

#include "rosmaster/rosmaster.hpp"
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

UsbWorker::UsbWorker(SharedState& sh, gateway::StopFlag& stop, UsbWorkerParams p)
  : sh_(sh), stop_(stop), p_(p) {}

void UsbWorker::operator()() {
  using clock = std::chrono::steady_clock;
  auto next_tp = clock::now();

  auto cfg_ptr = sh_.cfg.load(std::memory_order_acquire);

  rosmaster::Rosmaster bot;
  rosmaster::Config rcfg;
  rcfg.device = cfg_ptr ? cfg_ptr->serial_dev : "/dev/ttyUSB0";
  rcfg.baud   = cfg_ptr ? cfg_ptr->serial_baud : 115200;
  rcfg.debug  = false;

  if (!bot.connect(rcfg)) {
    logger::error() << "[USB] Failed to connect to " << rcfg.device << "@" << rcfg.baud << "\n";
    stop_.request_stop();
    return;
  }
  bot.start();
  bot.set_auto_report_state(true, false);

  uint32_t local_action_seq = 0;
  uint32_t state_seq = 0;

  auto stop_motors = [&]() {
    bot.set_motor(0, 0, 0, 0);
  };

  logger::info() << "[USB] Started.\n";

  while (!stop_.stop_requested()) {
    cfg_ptr = sh_.cfg.load(std::memory_order_acquire);
    const double usb_hz = cfg_ptr ? cfg_ptr->usb_hz : 200.0;

    core::Actions act = sh_.latest_action_request.load_or_default();

    // Safety: if system not running -> zero motors
    const auto sys = sh_.system_state.load_or_default();
    if (!sys.running) {
      act.motors = {};
      act.beep_ms = 0;
    }

    // Apply continuous motors only
    bot.set_motor(act.motors.m1, act.motors.m2, act.motors.m3, act.motors.m4);

    // Apply bounded one-shot HW events (e.g., beep) exactly once per event
    sh_.event_cmd_q.drain(p_.max_hw_events_per_cycle, [&](const gateway::EventCmd& ev) {
      if (ev.type == gateway::EventType::BEEP) {
        bot.set_beep(static_cast<int>(ev.data0));
      }

      EventSample es{};
      es.ts = now_timestamps();
      es.ev = ev;
      sh_.event_ring.push_overwrite(es);
    });

    // Read state and publish
    core::States st = bot.get_state();
    sh_.latest_state.store(st);

    StateSample ss{};
    ss.ts = now_timestamps();
    ss.seq = ++state_seq;
    ss.st = st;
    sh_.state_ring.push_overwrite(ss);

    // Log applied action (beep removed from continuous action log)
    ActionSample as{};
    as.ts = now_timestamps();
    as.seq = ++local_action_seq;
    as.act = act;
    as.act.beep_ms = 0;
    sh_.action_ring.push_overwrite(as);

    sleep_to_rate(usb_hz, next_tp);
  }

  // strict shutdown behavior
  stop_motors();
  bot.stop();
  bot.disconnect();

  logger::info() << "[USB] Stopped (motors zeroed).\n";
}

} // namespace workers
