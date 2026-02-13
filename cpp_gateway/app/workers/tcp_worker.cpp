#include "tcp_worker.hpp"

#include "connection/tcp_socket.hpp"
#include "connection/framed.hpp"
#include "connection/packets.hpp"
#include "utils/logger.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace workers {

static inline void sleep_to_rate(double hz, std::chrono::steady_clock::time_point& next_tp) {
  using clock = std::chrono::steady_clock;
  if (hz <= 0.0) hz = 1.0;
  const auto dt = std::chrono::duration<double>(1.0 / hz);
  next_tp += std::chrono::duration_cast<clock::duration>(dt);
  std::this_thread::sleep_until(next_tp);
}

static inline uint8_t rising_edges(uint8_t prev, uint8_t now) {
  return static_cast<uint8_t>((~prev) & now);
}

static inline void push_sys_event(SharedState& sh, uint32_t seq, uint8_t bit_index, uint8_t flags_snapshot) {
  gateway::EventCmd ev{};
  ev.type = gateway::EventType::FLAG_RISE;
  ev.seq  = seq;
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
  ev.type = gateway::EventType::BEEP;
  ev.seq  = seq;
  ev.data0 = beep_ms;
  sh.event_cmd_q.push_overwrite(ev);
}

static inline void emit_config_applied(SharedState& sh, uint32_t seq, uint8_t key) {
  gateway::EventCmd ev{};
  ev.type = gateway::EventType::CONFIG_APPLIED;
  ev.seq  = seq;
  ev.data0 = key;

  sh.sys_event_q.push_overwrite(ev);

  EventSample es{};
  es.ts = now_timestamps();
  es.ev = ev;
  sh.sys_event_ring.push_overwrite(es);
}

static void apply_config_pkt(SharedState& sh, const connection::ConfigPkt& cfgp) {
  auto cur = sh.cfg.load(std::memory_order_acquire);
  auto next = std::make_shared<gateway::RuntimeConfig>(cur ? *cur : gateway::RuntimeConfig{});

  // Key mapping (extend as needed; safe copy-modify-swap):
  // 1: usb_hz (u16)
  // 2: tcp_hz (u16)
  // 3: ctrl_hz (u16)
  // 10: flag_event_mask (u8)
  // 20: control_mode (u8)
  switch (cfgp.key) {
    case 1: next->usb_hz  = static_cast<double>(cfgp.u16); break;
    case 2: next->tcp_hz  = static_cast<double>(cfgp.u16); break;
    case 3: next->ctrl_hz = static_cast<double>(cfgp.u16); break;
    case 10: next->flag_event_mask = cfgp.u8; break;
    case 20: next->control_mode = static_cast<gateway::ControlMode>(cfgp.u8); break;
    default: break;
  }

  sh.cfg.store(next, std::memory_order_release);
  emit_config_applied(sh, cfgp.seq, cfgp.key);
}

TcpWorker::TcpWorker(SharedState& sh, gateway::StopFlag& stop)
  : sh_(sh), stop_(stop) {}

void TcpWorker::operator()() {
  using clock = std::chrono::steady_clock;
  auto next_tp = clock::now();

  auto cfg_ptr = sh_.cfg.load(std::memory_order_acquire);
  const std::string bind_ip = cfg_ptr ? cfg_ptr->bind_ip : "0.0.0.0";
  const uint16_t state_port = cfg_ptr ? cfg_ptr->state_port : 30001;
  const uint16_t cmd_port   = cfg_ptr ? cfg_ptr->cmd_port   : 30002;

  connection::TcpSocket srv_state;
  connection::TcpSocket srv_cmd;

  if (!srv_state.bind_listen(bind_ip, state_port, 8)) {
    logger::error() << "[TCP] Failed bind STATE " << bind_ip << ":" << state_port << "\n";
    stop_.request_stop();
    return;
  }
  srv_state.set_nonblocking(true);

  if (!srv_cmd.bind_listen(bind_ip, cmd_port, 2)) {
    logger::error() << "[TCP] Failed bind CMD " << bind_ip << ":" << cmd_port << "\n";
    stop_.request_stop();
    return;
  }
  srv_cmd.set_nonblocking(true);

  logger::info() << "[TCP] Listening STATE=" << bind_ip << ":" << state_port
                 << " CMD=" << bind_ip << ":" << cmd_port << "\n";

  std::vector<connection::TcpSocket> state_clients;

  connection::TcpSocket cmd_client;
  connection::FrameRx cmd_frx;

  uint32_t state_seq = 0;

  // Edge tracking per message type
  uint32_t last_cmd_seq = 0;
  uint8_t  last_cmd_flags = 0;
  uint32_t last_setpoint_seq = 0;
  uint8_t  last_setpoint_flags = 0;

  while (!stop_.stop_requested()) {
    cfg_ptr = sh_.cfg.load(std::memory_order_acquire);
    const double tcp_hz = cfg_ptr ? cfg_ptr->tcp_hz : 200.0;
    const uint8_t flag_event_mask = cfg_ptr ? cfg_ptr->flag_event_mask : 0x07;

    // Accept STATE clients
    while (true) {
      connection::TcpSocket c;
      if (!srv_state.accept_client(c, true)) break;
      state_clients.emplace_back(std::move(c));
    }

    // Accept CMD client (keep newest)
    while (true) {
      connection::TcpSocket c;
      if (!srv_cmd.accept_client(c, true)) break;

      if (cmd_client.is_open()) cmd_client.close();
      cmd_client = std::move(c);
      cmd_frx.clear();

      last_cmd_seq = 0; last_cmd_flags = 0;
      last_setpoint_seq = 0; last_setpoint_flags = 0;

      logger::info() << "[TCP] CMD client connected.\n";
    }

    // Broadcast STATE
    {
      auto st_opt = sh_.latest_state.load();
      if (st_opt) {
        const float t_mono_s = static_cast<float>(now_timestamps().mono_s);
        connection::StatesPkt sp = connection::state_to_state_pkt(++state_seq, t_mono_s, *st_opt);
        const auto hdr = connection::make_hdr(connection::MSG_STATE,
                                              static_cast<uint8_t>(sizeof(connection::StatesPkt)));

        uint8_t frame[sizeof(connection::MsgHdr) + sizeof(connection::StatesPkt)];
        std::memcpy(frame, &hdr, sizeof(hdr));
        std::memcpy(frame + sizeof(hdr), &sp, sizeof(sp));

        for (size_t i = 0; i < state_clients.size();) {
          if (!state_clients[i].is_open() || !state_clients[i].send_all(frame, sizeof(frame))) {
            state_clients[i].close();
            state_clients.erase(state_clients.begin() + static_cast<long>(i));
          } else {
            ++i;
          }
        }
      }
    }

    // Receive CMD frames
    if (cmd_client.is_open()) {
      uint8_t tmp[1024];
      size_t n = 0;

      if (cmd_client.try_recv(tmp, sizeof(tmp), n)) {
        if (n == 0) {
          cmd_client.close();
          logger::warn() << "[TCP] CMD client disconnected.\n";
        } else {
          cmd_frx.push_bytes(tmp, n);

          uint8_t type = 0;
          std::vector<uint8_t> payload;

          while (cmd_frx.pop(type, payload)) {
            const double now_mono = now_timestamps().mono_s;

            if (type == connection::MSG_CMD && payload.size() == sizeof(connection::CmdPkt)) {
              connection::CmdPkt pkt{};
              std::memcpy(&pkt, payload.data(), sizeof(pkt));

              sh_.last_cmd_rx_mono_s.store(now_mono, std::memory_order_release);

              // Beep one-shot, edge by seq (do not replay forever)
              if (pkt.seq != last_cmd_seq) {
                if (pkt.actions.beep_ms != 0) {
                  push_hw_beep_event(sh_, pkt.seq, pkt.actions.beep_ms);
                }
                last_cmd_seq = pkt.seq;
              }

              // Flags split: rising edge events on mask
              const uint8_t now_flags = pkt.actions.flags;
              const uint8_t rises = static_cast<uint8_t>(rising_edges(last_cmd_flags, now_flags) & flag_event_mask);
              if (rises) {
                for (uint8_t b = 0; b < 8; ++b) {
                  if (rises & (1u << b)) push_sys_event(sh_, pkt.seq, b, now_flags);
                }
              }
              last_cmd_flags = now_flags;

              // Continuous command path: clear beep + remove event bits
              core::Actions cont = pkt.actions;
              cont.beep_ms = 0;
              cont.flags = static_cast<uint8_t>(cont.flags & ~flag_event_mask);
              sh_.latest_remote_cmd.store(cont);

              // Update SystemState continuous flags
              auto sys = sh_.system_state.load_or_default();
              sys.continuous_flags = cont.flags;
              sh_.system_state.store(sys);
            }
            else if (type == connection::MSG_SETPOINT && payload.size() == sizeof(connection::SetpointPkt)) {
              connection::SetpointPkt sp{};
              std::memcpy(&sp, payload.data(), sizeof(sp));

              sh_.last_cmd_rx_mono_s.store(now_mono, std::memory_order_release);

              sh_.latest_setpoint_cmd.store(sp);

              // Flag split semantics for setpoint flags
              if (sp.seq != last_setpoint_seq) {
                const uint8_t now_flags = sp.flags;
                const uint8_t rises = static_cast<uint8_t>(rising_edges(last_setpoint_flags, now_flags) & flag_event_mask);
                if (rises) {
                  for (uint8_t b = 0; b < 8; ++b) {
                    if (rises & (1u << b)) push_sys_event(sh_, sp.seq, b, now_flags);
                  }
                }
                last_setpoint_flags = now_flags;
                last_setpoint_seq = sp.seq;
              }

              auto sys = sh_.system_state.load_or_default();
              sys.continuous_flags = static_cast<uint8_t>(sp.flags & ~flag_event_mask);
              sh_.system_state.store(sys);
            }
            else if (type == connection::MSG_CONFIG && payload.size() == sizeof(connection::ConfigPkt)) {
              connection::ConfigPkt cp{};
              std::memcpy(&cp, payload.data(), sizeof(cp));
              apply_config_pkt(sh_, cp);
            }
            else {
              // Ignore unknown / size mismatch
            }
          }
        }
      }
    }

    sleep_to_rate(tcp_hz, next_tp);
  }

  if (cmd_client.is_open()) cmd_client.close();
  for (auto& c : state_clients) c.close();
  srv_state.close();
  srv_cmd.close();

  logger::info() << "[TCP] Stopped.\n";
}

} // namespace workers
