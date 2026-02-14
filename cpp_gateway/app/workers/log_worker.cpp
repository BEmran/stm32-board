#include "log_worker.hpp"

#include "utils/rotating_binary_log.hpp"
#include "utils/logger.hpp"

#include <chrono>
#include <thread>

namespace workers {

static inline void sleep_for_ms(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

LogWorker::LogWorker(SharedState& sh, gateway::StopFlag& stop)
  : sh_(sh), stop_(stop) {}

void LogWorker::operator()() {
  utils::RotatingBinaryLog writer;

  auto cfg_ptr = sh_.cfg.load(std::memory_order_acquire);
  const bool binlog = cfg_ptr ? cfg_ptr->binary_log : true;
  const std::string path = cfg_ptr ? cfg_ptr->log_path : "./logs/gateway.bin";

  if (binlog) {
    const uint64_t max_bytes = cfg_ptr ? (static_cast<uint64_t>(cfg_ptr->log_rotate_mb) * 1024ULL * 1024ULL) : (256ULL * 1024ULL * 1024ULL);
    const uint32_t keep = cfg_ptr ? cfg_ptr->log_rotate_keep : 10U;
    if (!writer.open(path, max_bytes, keep)) {
      logger::warn() << "[LOG] Failed to open rotating binary log: " << path << "\n";
    }
  }

  using clock = std::chrono::steady_clock;
  auto last_warn = clock::now();
  auto last_info = clock::now();

  uint64_t last_state_d = 0, last_cmd_d = 0, last_event_d = 0, last_sys_event_d = 0;
  uint64_t last_event_q_d = 0, last_sys_q_d = 0;

  while (!stop_.stop_requested()) {
    // Drain rings (disk I/O only)
    sh_.state_ring.drain(1024, [&](const StateSample& s) {
      if (!writer.is_open()) return;
      utils::RecordHeader h{};
      h.type = utils::RecordType::STATE;
      h.epoch_s = s.ts.epoch_s;
      h.mono_s  = s.ts.mono_s;
      writer.write_record(h, &s, static_cast<uint16_t>(sizeof(s)));
    });

    sh_.cmd_ring.drain(1024, [&](const MotorCommandsSample& a) {
      if (!writer.is_open()) return;
      utils::RecordHeader h{};
      h.type = utils::RecordType::CMD;
      h.epoch_s = a.ts.epoch_s;
      h.mono_s  = a.ts.mono_s;
      writer.write_record(h, &a, static_cast<uint16_t>(sizeof(a)));
    });

    // event_ring (HW events)
    sh_.event_ring.drain(1024, [&](const EventSample& e) {
      if (!writer.is_open()) return;
      utils::RecordHeader h{};
      h.type = utils::RecordType::EVENT;
      h.epoch_s = e.ts.epoch_s;
      h.mono_s  = e.ts.mono_s;
      writer.write_record(h, &e, static_cast<uint16_t>(sizeof(e)));
    });

    // sys_event_ring is also logged as EVENT (per requirement: only STATE/ACTION/EVENT)
    sh_.sys_event_ring.drain(1024, [&](const EventSample& e) {
      if (!writer.is_open()) return;
      utils::RecordHeader h{};
      h.type = utils::RecordType::EVENT;
      h.epoch_s = e.ts.epoch_s;
      h.mono_s  = e.ts.mono_s;
      writer.write_record(h, &e, static_cast<uint16_t>(sizeof(e)));
    });

    // Drop warnings <= 1 Hz (explicit names)
    const auto now = clock::now();
    if (std::chrono::duration<double>(now - last_warn).count() >= 1.0) {
      last_warn = now;

      const uint64_t sd = sh_.state_ring.drops();
      const uint64_t ad = sh_.cmd_ring.drops();
      const uint64_t ed = sh_.event_ring.drops();
      const uint64_t xd = sh_.sys_event_ring.drops();
      const uint64_t eqd = sh_.event_cmd_q.drops();
      const uint64_t sqd = sh_.sys_event_q.drops();

      if (sd != last_state_d) logger::warn() << "[DROP] state_ring=" << sd << "\n";
      if (ad != last_cmd_d) logger::warn() << "[DROP] cmd_ring=" << ad << "\n";
      if (ed != last_event_d) logger::warn() << "[DROP] event_ring=" << ed << "\n";
      if (xd != last_sys_event_d) logger::warn() << "[DROP] sys_event_ring=" << xd << "\n";
      if (eqd != last_event_q_d) logger::warn() << "[DROP] event_cmd_q=" << eqd << "\n";
      if (sqd != last_sys_q_d) logger::warn() << "[DROP] sys_event_q=" << sqd << "\n";

      last_state_d = sd; last_cmd_d = ad; last_event_d = ed; last_sys_event_d = xd;
      last_event_q_d = eqd; last_sys_q_d = sqd;
    }


    // Periodic health summary (keep sparse).
    if (std::chrono::duration<double>(now - last_info).count() >= 5.0) {
      last_info = now;

      const uint64_t sd2 = sh_.state_ring.drops();
      const uint64_t ad2 = sh_.cmd_ring.drops();
      const uint64_t ed2 = sh_.event_ring.drops();
      const uint64_t xd2 = sh_.sys_event_ring.drops();
      const uint64_t eqd2 = sh_.event_cmd_q.drops();
      const uint64_t sqd2 = sh_.sys_event_q.drops();

      const auto cfg = sh_.cfg.load(std::memory_order_acquire);
      const double timeout_s = cfg ? cfg->cmd_timeout_s : 0.2;
      const double last_cmd = sh_.last_cmd_rx_mono_s.load(std::memory_order_acquire);

      double age_s = -1.0;
      if (last_cmd > 0.0) {
        age_s = utils::now().mono_s - last_cmd;
      }

      logger::info() << "[HEALTH] drops: state=" << sd2
                     << " cmd=" << ad2
                     << " event=" << ed2
                     << " sys_event=" << xd2
                     << " q(event)=" << eqd2
                     << " q(sys)=" << sqd2
                     << " | cmd_age=" << age_s << "s (timeout=" << timeout_s << "s)\n";
    }

    sleep_for_ms(5);
  }

  writer.close();
  logger::info() << "[LOG] Stopped.\n";
}

} // namespace workers
