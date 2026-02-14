#include "tcp_worker.hpp"

#include "connection/framed.hpp"
#include "connection/tcp_socket.hpp"
#include "connection/wire_codec.hpp"
#include "utils/logger.hpp"
#include "utils/rate_limiter.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace workers {

// ---- small helpers ----
static inline uint8_t rising_edges(uint8_t prev, uint8_t now) {
  return static_cast<uint8_t>((~prev) & now);
}

static inline void push_sys_event(SharedState& sh, uint32_t seq, uint8_t bit_index, uint8_t flags_snapshot) {
  gateway::EventCmd ev{};
  ev.type  = gateway::EventType::FLAG_RISE;
  ev.seq   = seq;
  ev.data0 = bit_index;
  ev.data1 = flags_snapshot;

  sh.sys_event_q.push_overwrite(ev);

  EventSample es{};
  es.ts = now_timestamps();
  es.ev = ev;
  sh.sys_event_ring.push_overwrite(es);
}

static inline void push_hw_beep_event(SharedState& sh, uint32_t seq, uint8_t beep_ms) {
  gateway::EventCmd ev{};
  ev.type  = gateway::EventType::BEEP;
  ev.seq   = seq;
  ev.data0 = beep_ms;
  sh.event_cmd_q.push_overwrite(ev);
}

static inline void emit_config_applied(SharedState& sh, uint32_t seq, uint8_t key) {
  gateway::EventCmd ev{};
  ev.type  = gateway::EventType::CONFIG_APPLIED;
  ev.seq   = seq;
  ev.data0 = key;

  sh.sys_event_q.push_overwrite(ev);

  EventSample es{};
  es.ts = now_timestamps();
  es.ev = ev;
  sh.sys_event_ring.push_overwrite(es);
}

static inline double clampd(double v, double lo, double hi) noexcept {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline bool is_valid_control_mode(uint8_t m) noexcept {
  return m <= static_cast<uint8_t>(gateway::ControlMode::AUTONOMOUS_WITH_REMOTE_SETPOINT);
}

static inline bool is_valid_usb_timeout_mode(uint8_t m) noexcept {
  return m <= static_cast<uint8_t>(gateway::UsbTimeoutMode::DISABLE);
}

static void apply_config_payload(SharedState& sh, const connection::wire::ConfigPayload& cfgp) {
  auto cur  = sh.cfg.load(std::memory_order_acquire);
  auto next = std::make_shared<gateway::RuntimeConfig>(cur ? *cur : gateway::RuntimeConfig{});

  // Key mapping (extend as needed; safe copy-modify-swap):
  // 1: usb_hz (u16)
  // 2: tcp_hz (u16)
  // 3: ctrl_hz (u16)
  // 4: cmd_timeout_ms (u16)
  // 5: usb_timeout_mode (u8)
  // 6: log_rotate_mb (u16)
  // 7: log_rotate_keep (u16)
  // 10: flag_event_mask (u8)
  // 20: control_mode (u8)
  // 30: ctrl_thread_priority (i16 packed into u16)
  switch (cfgp.key) {
    case 1: next->usb_hz = clampd(static_cast<double>(cfgp.u16), 1.0, 2000.0); break;
    case 2: next->tcp_hz = clampd(static_cast<double>(cfgp.u16), 1.0, 2000.0); break;
    case 3: next->ctrl_hz = clampd(static_cast<double>(cfgp.u16), 1.0, 2000.0); break;
    case 4: next->cmd_timeout_s = clampd(static_cast<double>(cfgp.u16) / 1000.0, 0.01, 5.0); break;
    case 5:
      if (is_valid_usb_timeout_mode(cfgp.u8)) next->usb_timeout_mode = static_cast<gateway::UsbTimeoutMode>(cfgp.u8);
      break;
    case 6:
      next->log_rotate_mb = static_cast<uint32_t>(clampd(static_cast<double>(cfgp.u16), 1.0, 8192.0));
      break;
    case 7:
      next->log_rotate_keep = static_cast<uint32_t>(clampd(static_cast<double>(cfgp.u16), 1.0, 200.0));
      break;
    case 10:
      next->flag_event_mask = cfgp.u8;
      break;
    case 20:
      if (is_valid_control_mode(cfgp.u8)) next->control_mode = static_cast<gateway::ControlMode>(cfgp.u8);
      break;
    case 30:
      next->ctrl_thread_priority = static_cast<int16_t>(cfgp.u16);
      break;
    default:
      // unknown key => ignore
      break;
  }

  sh.cfg.store(next, std::memory_order_release);
  emit_config_applied(sh, cfgp.seq, cfgp.key);
}

// Build stats response payload. Keep it small and fixed-size.
static connection::wire::StatsPayload build_stats(SharedState& sh, uint32_t seq) {
  connection::wire::StatsPayload s{};
  s.seq = seq;

  const double now_mono = now_timestamps().mono_s;
  s.uptime_ms = static_cast<uint32_t>( (now_mono - sh.start_mono_s) * 1000.0 );

  if (auto cfg = sh.cfg.load(std::memory_order_acquire)) {
    s.usb_hz  = static_cast<float>(cfg->usb_hz);
    s.tcp_hz  = static_cast<float>(cfg->tcp_hz);
    s.ctrl_hz = static_cast<float>(cfg->ctrl_hz);
  }

  s.drops_state     = static_cast<uint32_t>(sh.state_ring.drops());
  s.drops_action    = static_cast<uint32_t>(sh.action_ring.drops());
  s.drops_event     = static_cast<uint32_t>(sh.event_ring.drops());
  s.drops_sys_event = static_cast<uint32_t>(sh.sys_event_ring.drops());
  s.tcp_frames_bad  = sh.tcp_frames_bad.load(std::memory_order_relaxed);
  s.serial_errors   = sh.serial_errors.load(std::memory_order_relaxed);
  return s;
}

// ---- worker ----
TcpWorker::TcpWorker(SharedState& sh, gateway::StopFlag& stop)
  : sh_(sh), stop_(stop) {}

void TcpWorker::operator()() {
  // Sockets
  connection::TcpSocket state_srv;
  connection::TcpSocket cmd_srv;

  auto cfg = sh_.cfg.load(std::memory_order_acquire);
  const std::string bind_ip = cfg ? cfg->bind_ip : "0.0.0.0";
  const uint16_t state_port = cfg ? cfg->state_port : 30001;
  const uint16_t cmd_port   = cfg ? cfg->cmd_port : 30002;

  // Ignore SIGPIPE behavior is set in main.

  if (!state_srv.bind_listen(bind_ip, state_port, 4)) {
    logger::warn() << "[TCP] Failed to bind state server on " << bind_ip << ":" << state_port << "\n";
  } else {
    logger::info() << "[TCP] State server listening on " << bind_ip << ":" << state_port << "\n";
    // IMPORTANT: server sockets must be non-blocking, otherwise accept() will block the whole worker
    // and prevent state publishing and clean shutdown.
    (void)state_srv.set_nonblocking(true);
  }

  if (!cmd_srv.bind_listen(bind_ip, cmd_port, 4)) {
    logger::warn() << "[TCP] Failed to bind cmd server on " << bind_ip << ":" << cmd_port << "\n";
  } else {
    logger::info() << "[TCP] Cmd server listening on " << bind_ip << ":" << cmd_port << "\n";
    (void)cmd_srv.set_nonblocking(true);
  }

  std::vector<connection::TcpSocket> state_clients;
  std::vector<connection::TcpSocket> cmd_clients;

  connection::FrameRx cmd_frx;

  // Tracking for legacy CMD beep/event edges
  uint32_t last_cmd_seq = 0;
  uint8_t  last_cmd_flags = 0;

  uint32_t last_sp_seq = 0;

  uint32_t state_seq = 0;
  uint32_t stats_seq = 0;

  utils::RateLimiter rate(cfg ? cfg->tcp_hz : 200.0);

  while (!stop_.stop_requested()) {
    // Hot-reload rate
    if (auto cfg2 = sh_.cfg.load(std::memory_order_acquire)) {
      rate.set_hz(cfg2->tcp_hz);
    }
    rate.sleep();

    // Accept any new clients (non-blocking).
    {
      connection::TcpSocket c;
      while (state_srv.is_open() && state_srv.accept_client(c, true)) {
        c.set_nonblocking(true);
        state_clients.emplace_back(std::move(c));
        logger::info() << "[TCP] State client connected (" << state_clients.size() << ")\n";
      }
    }
    {
      connection::TcpSocket c;
      while (cmd_srv.is_open() && cmd_srv.accept_client(c, true)) {
        c.set_nonblocking(true);
        cmd_clients.emplace_back(std::move(c));
        logger::info() << "[TCP] Cmd client connected (" << cmd_clients.size() << ")\n";
      }
    }

    // ---- Receive + route commands ----
    for (size_t i = 0; i < cmd_clients.size();) {
      if (!cmd_clients[i].is_open()) {
        cmd_clients.erase(cmd_clients.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }

      uint8_t buf[2048];
      size_t n = 0;
      if (!cmd_clients[i].try_recv(buf, sizeof(buf), n)) {
        cmd_clients[i].close();
        cmd_clients.erase(cmd_clients.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }

      if (n > 0) {
        cmd_frx.push_bytes(buf, n);
      }

      bool progressed = false;
      uint8_t type = 0;
      std::vector<uint8_t> payload;
      while (cmd_frx.pop(type, payload)) {
        progressed = true;
        const double now_mono = now_timestamps().mono_s;

        if (type == connection::MSG_CMD) {
          connection::wire::CmdPayload cp{};
          if (!connection::wire::decode_cmd_payload(payload, cp)) {
            sh_.tcp_frames_bad.fetch_add(1, std::memory_order_relaxed);
            continue;
          }

          sh_.last_cmd_rx_mono_s.store(now_mono, std::memory_order_release);

          // Beep is one-shot based on seq (do not replay forever)
          if (cp.seq != last_cmd_seq) {
            if (cp.actions.beep_ms != 0) {
              push_hw_beep_event(sh_, cp.seq, cp.actions.beep_ms);
              // Clear beep so it doesn't repeat as continuous command.
              cp.actions.beep_ms = 0;
            }

            // Rising-edge events for flags (continuous flags are handled by controller/USB)
            const uint8_t rises = rising_edges(last_cmd_flags, cp.actions.flags);
            if (rises != 0) {
              const uint8_t mask = (sh_.cfg.load(std::memory_order_acquire) ? sh_.cfg.load(std::memory_order_acquire)->flag_event_mask : 0x07);
              const uint8_t eff = static_cast<uint8_t>(rises & mask);
              for (uint8_t b = 0; b < 8; ++b) {
                if (eff & (1u << b)) push_sys_event(sh_, cp.seq, b, cp.actions.flags);
              }
            }

            last_cmd_seq = cp.seq;
            last_cmd_flags = cp.actions.flags;
          }

          sh_.latest_remote_cmd.store(cp.actions);
        }
        else if (type == connection::MSG_SETPOINT) {
          connection::wire::SetpointPayload sp{};
          if (!connection::wire::decode_setpoint_payload(payload, sp)) {
            sh_.tcp_frames_bad.fetch_add(1, std::memory_order_relaxed);
            continue;
          }

          sh_.last_cmd_rx_mono_s.store(now_mono, std::memory_order_release);

          if (sp.seq != last_sp_seq) {
            last_sp_seq = sp.seq;
          }
          sh_.latest_setpoint_cmd.store(sp);
        }
        else if (type == connection::MSG_CONFIG) {
          connection::wire::ConfigPayload cfgp{};
          if (!connection::wire::decode_config_payload(payload, cfgp)) {
            sh_.tcp_frames_bad.fetch_add(1, std::memory_order_relaxed);
            continue;
          }

          apply_config_payload(sh_, cfgp);
        }
        else if (type == connection::MSG_STATS_REQ) {
          // len=0 request; respond with fixed payload
          (void)payload;
          const auto stats = build_stats(sh_, ++stats_seq);

          connection::MsgHdr hdr = connection::make_hdr(connection::MSG_STATS_RESP,
                                                       static_cast<uint8_t>(connection::wire::kStatsPayloadSize));
          uint8_t frame[sizeof(connection::MsgHdr) + connection::wire::kStatsPayloadSize];
          std::memcpy(frame, &hdr, sizeof(hdr));
          (void)connection::wire::encode_stats_payload(std::span<uint8_t>(frame + sizeof(hdr),
                                                                          connection::wire::kStatsPayloadSize), stats);

          (void)cmd_clients[i].send_all(frame, sizeof(frame)); // best-effort
        }
        else {
          sh_.tcp_frames_bad.fetch_add(1, std::memory_order_relaxed);
        }
      }

      // If we consumed bytes without emitting frames due to resync, count as bad slowly.
      if (!progressed && n > 0) {
        // Heuristic: only count if buffer is large (meaning it wasn't just partial frame)
        // (FrameRx already protects against runaway).
      }

      ++i;
    }

    // ---- Publish STATE frames ----
    auto st_opt = sh_.latest_state.load();
    if (st_opt) {
      const float t_mono_s = static_cast<float>(now_timestamps().mono_s);

      const auto hdr = connection::make_hdr(connection::MSG_STATE,
                                            static_cast<uint8_t>(connection::wire::kStatesPayloadSize));

      uint8_t frame[sizeof(connection::MsgHdr) + connection::wire::kStatesPayloadSize];
      std::memcpy(frame, &hdr, sizeof(hdr));
      connection::wire::encode_states_payload(std::span<uint8_t>(frame + sizeof(hdr),
                                                                 connection::wire::kStatesPayloadSize),
                                              ++state_seq, t_mono_s, *st_opt);

      for (size_t i = 0; i < state_clients.size();) {
        if (!state_clients[i].is_open()) {
          state_clients.erase(state_clients.begin() + static_cast<std::ptrdiff_t>(i));
          continue;
        }

        if (!state_clients[i].send_all(frame, sizeof(frame))) {
          state_clients[i].close();
          state_clients.erase(state_clients.begin() + static_cast<std::ptrdiff_t>(i));
          continue;
        }
        ++i;
      }
    }
  }

  // Cleanup sockets
  for (auto& c : state_clients) c.close();
  for (auto& c : cmd_clients) c.close();
  state_srv.close();
  cmd_srv.close();

  logger::info() << "[TCP] Worker exit\n";
}

} // namespace workers
