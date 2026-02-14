#include "usb_worker.hpp"

#include "rosmaster/rosmaster.hpp"
#include "utils/logger.hpp"
#include "utils/rate_limiter.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace workers {

namespace {
constexpr int kShutdownMotorOffBurst = 5;
constexpr auto kShutdownMotorOffSpacing = std::chrono::milliseconds(10);

constexpr auto kConnectBackoffMin = std::chrono::milliseconds(200);
constexpr auto kConnectBackoffMax = std::chrono::milliseconds(1000);
constexpr auto kConnectMaxTotal   = std::chrono::seconds(5);

inline std::chrono::milliseconds backoff_for_attempt(int attempt) {
  // 200ms, 400ms, 800ms, 1000ms, 1000ms...
  const int shift = std::clamp(attempt, 0, 3);
  auto d = kConnectBackoffMin * (1 << shift);
  if (d > kConnectBackoffMax) d = kConnectBackoffMax;
  return d;
}
} // namespace

UsbWorker::UsbWorker(SharedState& sh, gateway::StopFlag& stop, UsbWorkerParams p)
  : sh_(sh), stop_(stop), p_(p) {}

void UsbWorker::operator()() {
  utils::RateLimiter rl;

  auto cfg_ptr = sh_.cfg.load(std::memory_order_acquire);
  rl.set_hz(cfg_ptr ? cfg_ptr->usb_hz : 200.0);
  rl.reset();

  rosmaster::Rosmaster bot;
  rosmaster::Config rcfg;
  rcfg.device = cfg_ptr ? cfg_ptr->serial_dev : "/dev/ttyUSB0";
  rcfg.baud   = cfg_ptr ? cfg_ptr->serial_baud : 115200;
  rcfg.debug  = false;

  auto connect_or_request_stop = [&]() -> bool {
    const auto t0 = std::chrono::steady_clock::now();
    int attempt = 0;

    while (!stop_.stop_requested()) {
      if (bot.connect(rcfg)) return true;

      const auto elapsed = std::chrono::steady_clock::now() - t0;
      if (elapsed >= kConnectMaxTotal) break;

      const auto d = backoff_for_attempt(attempt++);
      logger::warn() << "[USB] Connect failed (" << rcfg.device << "@" << rcfg.baud
                     << "). Retrying in " << d.count() << " ms...\n";
      std::this_thread::sleep_for(d);
    }

    // USB is mandatory: if we cannot connect in a bounded time, stop the gateway.
    logger::error() << "[USB] Failed to connect to " << rcfg.device << "@" << rcfg.baud
                    << " (USB mandatory). Requesting stop.\n";
    stop_.request_stop();
    return false;
  };

  if (!connect_or_request_stop()) return;

  if (!bot.start()) {
    sh_.serial_errors.fetch_add(1, std::memory_order_relaxed);
    logger::error() << "[USB] Failed to start RX thread. Requesting stop.\n";
    stop_.request_stop();
    return;
  }
  bot.set_auto_report_state(true, false);

  uint32_t local_action_seq = 0;
  uint32_t state_seq = 0;

  auto stop_motors_burst = [&]() {
    for (int i = 0; i < kShutdownMotorOffBurst; ++i) {
      bot.set_motor(0, 0, 0, 0);
      std::this_thread::sleep_for(kShutdownMotorOffSpacing);
    }
  };

  bool was_timeout = false;
  double last_timeout_log_mono = 0.0;

  logger::info() << "[USB] Started.\n";

  while (!stop_.stop_requested()) {
    cfg_ptr = sh_.cfg.load(std::memory_order_acquire);
    const double usb_hz = cfg_ptr ? cfg_ptr->usb_hz : 200.0;

    const auto ts = now_timestamps();
    const double now_mono = ts.mono_s;

    core::Actions act = sh_.latest_action_request.load_or_default();

    // Safety: if system not running -> zero motors.
    const auto sys = sh_.system_state.load_or_default();
    if (!sys.running) {
      act.motors = {};
      act.beep_ms = 0;
    }

    // P0 safety: USB-side watchdog is mandatory.
    // Even if the controller thread stalls, USB must not keep applying stale motor commands.
    const double last_cmd_rx = sh_.last_cmd_rx_mono_s.load(std::memory_order_acquire);
    const double timeout_s = cfg_ptr ? cfg_ptr->cmd_timeout_s : 0.2;
    const auto timeout_mode = cfg_ptr ? cfg_ptr->usb_timeout_mode : gateway::UsbTimeoutMode::ENFORCE;

    const bool cmd_fresh = (timeout_mode == gateway::UsbTimeoutMode::DISABLE) ||
                           (last_cmd_rx > 0.0 && (now_mono - last_cmd_rx) <= timeout_s);

    const bool timed_out = !cmd_fresh;
    if (timed_out) {
      act.motors = {};
      act.beep_ms = 0;

      // throttle warning logs (1 Hz)
      if ((!was_timeout) || (now_mono - last_timeout_log_mono) >= 1.0) {
        logger::warn() << "[USB] Command timeout (" << (now_mono - last_cmd_rx)
                       << "s since last cmd). Motors forced to zero.\n";
        last_timeout_log_mono = now_mono;
      }
    }
    was_timeout = timed_out;

    // Apply continuous motors only.
    if (!bot.set_motor(act.motors.m1, act.motors.m2, act.motors.m3, act.motors.m4)) {
      sh_.serial_errors.fetch_add(1, std::memory_order_relaxed);
      logger::error() << "[USB] set_motor() failed. USB mandatory => stopping.\n";
      stop_.request_stop();
      break;
    }

    // Apply bounded one-shot HW events (e.g., beep) exactly once per event.
    sh_.event_cmd_q.drain(p_.max_hw_events_per_cycle, [&](const gateway::EventCmd& ev) {
      if (ev.type == gateway::EventType::BEEP) {
        if (!bot.set_beep(static_cast<int>(ev.data0))) {
          sh_.serial_errors.fetch_add(1, std::memory_order_relaxed);
          logger::warn() << "[USB] set_beep() failed.\n";
        }
      }

      EventSample es{};
      es.ts = ts;
      es.ev = ev;
      sh_.event_ring.push_overwrite(es);
    });

    // Read state and publish.
    core::States st = bot.get_state();
    sh_.latest_state.store(st);

    StateSample ss{};
    ss.ts = ts;
    ss.seq = ++state_seq;
    ss.st = st;
    sh_.state_ring.push_overwrite(ss);

    // Log applied action (beep removed from continuous action log).
    ActionSample as{};
    as.ts = ts;
    as.seq = ++local_action_seq;
    as.act = act;
    as.act.beep_ms = 0;
    sh_.action_ring.push_overwrite(as);

    rl.set_hz(usb_hz);
    rl.sleep();
  }

  // Strict shutdown behavior: attempt multiple motor-off sends to reduce "last write lost" risk.
  stop_motors_burst();
  bot.stop();
  bot.disconnect();

  logger::info() << "[USB] Stopped (motors zeroed).\n";
}

} // namespace workers
