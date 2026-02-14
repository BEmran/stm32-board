#include "connection/tcp_socket.hpp"
#include "connection/packets.hpp"
#include "connection/framed.hpp"
#include "helpper.hpp"
#include "rosmaster/rosmaster.hpp"
#include "utils/logger.hpp"
#include "utils/csv_recorder.hpp"
#include "utils/timestamp.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

constexpr int SERIAL_BAUD{115200};
constexpr double PRINT_DURATION = 1.0;
using namespace std::chrono_literals;
constexpr std::string_view RECORDER_PATH{"./recorder"};
constexpr uint16_t DEFAULT_STATE_PORT{30001};
constexpr uint16_t DEFAULT_CMD_PORT{30002};

constexpr double CMD_TIMEOUT{0.2};     // if no cmd received, stop motors
constexpr int STATE_PUBLISH_FREQ{200}; // Hz
constexpr const char *SERIAL_DEV{"/dev/ttyUSB0"};
constexpr const char *BIND_IP{"0.0.0.0"};

static std::atomic<bool> g_run{true};
static void on_sigint(int) { g_run.store(false); }

using namespace std::chrono_literals;

struct Config {
  std::string serial_dev{SERIAL_DEV};
  int serial_baud{SERIAL_BAUD};
  std::string bind_ip{BIND_IP};

  uint16_t state_port{DEFAULT_STATE_PORT};
  uint16_t cmd_port{DEFAULT_CMD_PORT};

  double hz{STATE_PUBLISH_FREQ};
  double cmd_timeout_s{CMD_TIMEOUT};

  // log throttles
  double motor_log_hz{10.0};
};

static bool parse_config(int argc, char **argv, Config &config) {
  // Split TCP ports:
  //   STATE: server -> clients (broadcast)
  //   CMD  : client -> server (single active controller)
  //
  // Usage:
  //   --serial /dev/ttyUSB0 --baud 115200
  //   --bind_ip 0.0.0.0 --state_port 30001 --cmd_port 30002
  //   --hz 200 --cmd_timeout 0.2
  //
  // Back-compat:
  //   --port is treated as --state_port
  for (int i = 1; i < argc; i++) {
    std::string_view a = argv[i];
    auto need = [&](std::string_view name) -> std::string_view {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };

    if (a == "--serial") config.serial_dev = std::string(need("--serial"));
    else if (a == "--baud") config.serial_baud = std::stoi(std::string(need("--baud")));
    else if (a == "--bind_ip") config.bind_ip = std::string(need("--bind_ip"));

    else if (a == "--state_port") config.state_port = static_cast<uint16_t>(std::stoi(std::string(need("--state_port"))));
    else if (a == "--cmd_port") config.cmd_port = static_cast<uint16_t>(std::stoi(std::string(need("--cmd_port"))));
    else if (a == "--port") config.state_port = static_cast<uint16_t>(std::stoi(std::string(need("--port")))); // back-compat
    else if (a == "--hz") config.hz = std::stod(std::string(need("--hz")));
    else if (a == "--cmd_timeout") config.cmd_timeout_s = std::stod(std::string(need("--cmd_timeout")));
    else if (a == "--motor_log_hz") config.motor_log_hz = std::stod(std::string(need("--motor_log_hz")));

    else if (a == "--help") {
      logger::info()
          << "Usage: " << argv[0] << " [options]\n"
          << "  --serial /dev/ttyUSB0      Serial device\n"
          << "  --baud 115200              Serial baud\n"
          << "  --bind_ip 0.0.0.0          Local bind IP\n"
          << "  --state_port 30001         TCP STATE port (server -> clients)\n"
          << "  --cmd_port 30002           TCP CMD port (client -> server)\n"
          << "  --hz 200                   STATE publish rate\n"
          << "  --cmd_timeout 0.2          Command timeout (seconds)\n"
          << "  --motor_log_hz 10          Motor log rate (Hz, 0=off)\n"
          << "\nBack-compat:\n"
          << "  --port N                   Treated as --state_port N\n";
      return false;
    } else {
      logger::error() << "Unknown arg: " << a << "\n";
      return false;
    }
  }

  if (config.hz <= 0.0) config.hz = static_cast<double>(STATE_PUBLISH_FREQ);
  if (config.cmd_timeout_s <= 0.0) config.cmd_timeout_s = CMD_TIMEOUT;
  if (config.state_port == 0 || config.cmd_port == 0) {
    logger::error() << "Invalid port(s).\n";
    return false;
  }
  return true;
}

int main(int argc, char **argv) {
  Config config;
  if (!parse_config(argc, argv, config))
    return 0;

  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif

  utils::CSVCommandRecorder commands_recorder(RECORDER_PATH);
  utils::CSVStatesRecorder states_recorder(RECORDER_PATH);
  if (!commands_recorder.open() || !states_recorder.open()) {
    logger::error() << "[TCP_GW] Failed to open CSV recorders\n";
    return 1;
  }
  // ---- Rosmaster ----
  rosmaster::Rosmaster bot;
  rosmaster::Config cfg;
  cfg.device = config.serial_dev;
  cfg.baud = config.serial_baud;
  cfg.debug = false;

  if (!bot.connect(cfg)) {
    logger::error() << "[TCP_GW] Failed to connect to " << cfg.device << "\n";
    return 1;
  }
  bot.start();
  bot.set_auto_report_state(true, false);

  // ---- STATE server (multi clients) ----
  connection::TcpSocket srv_state;
  if (!srv_state.bind_listen(config.bind_ip, config.state_port, /*backlog=*/8)) {
    logger::error() << "[TCP_GW] Failed to bind STATE on " << config.bind_ip
                    << ":" << config.state_port << "\n";
    return 1;
  }
  srv_state.set_nonblocking(true);

  // ---- CMD server (single active control client) ----
  connection::TcpSocket srv_cmd;
  if (!srv_cmd.bind_listen(config.bind_ip, config.cmd_port, /*backlog=*/2)) {
    logger::error() << "[TCP_GW] Failed to bind CMD on " << config.bind_ip
                    << ":" << config.cmd_port << "\n";
    return 1;
  }
  srv_cmd.set_nonblocking(true);

  logger::info()
      << "[TCP_GW] Serial=" << config.serial_dev << "@" << config.serial_baud
      << " | STATE: " << config.bind_ip << ":" << config.state_port
      << " | CMD: " << config.bind_ip << ":" << config.cmd_port
      << " | rate=" << config.hz << " Hz"
      << " | cmd_timeout=" << config.cmd_timeout_s << " s\n";

  std::vector<connection::TcpSocket> state_clients;

  connection::TcpSocket cmd_client; // only one controller at a time
  connection::FrameRx cmd_frx;

  using clock = std::chrono::steady_clock;
  const auto dt = std::chrono::duration<double>(1.0 / config.hz);
  const auto t0 = clock::now();
  auto next = clock::now();

  connection::MotorCmdPkt last_cmd{};
  bool have_cmd = false;
  bool last_cmd_valid = false;
  auto last_cmd_time = clock::now();

  uint32_t state_seq = 0;

  auto last_motor_log = clock::now() - 10s;
  helpper::Print print(1.0);
  while (g_run.load()) {
    // ---- accept STATE clients (non-blocking, drain accept queue) ----
    while (true) {
      connection::TcpSocket c;
      if (!srv_state.accept_client(c, /*nonblocking=*/true))
        break;

      logger::info() << "[TCP_GW] STATE client connected.\n";
      state_clients.emplace_back(std::move(c));
    }

    // ---- accept CMD client (non-blocking, keep only newest) ----
    while (true) {
      connection::TcpSocket c;
      if (!srv_cmd.accept_client(c, /*nonblocking=*/true))
        break;

      if (cmd_client.is_open()) {
        cmd_client.close();
        logger::warn() << "[TCP_GW] CMD client replaced (new controller connected).\n";
      }
      cmd_client = std::move(c);
      cmd_frx.clear();
      have_cmd = false;
      last_cmd_valid = false;
      last_cmd_time = clock::now();
      logger::info() << "[TCP_GW] CMD client connected.\n";
    }

    // ---- receive framed CMD messages (non-blocking) ----
    if (cmd_client.is_open()) {
      uint8_t tmp[1024];
      size_t n = 0;
      if (cmd_client.try_recv(tmp, sizeof(tmp), n)) {
        if (n == 0) {
          cmd_client.close();
          logger::warn() << "[TCP_GW] CMD client disconnected.\n";
        } else {
          cmd_frx.push_bytes(tmp, n);

          uint8_t type = 0;
          std::vector<uint8_t> payload;
          while (cmd_frx.pop(type, payload)) {
            if (type == connection::MSG_CMD && payload.size() == sizeof(connection::MotorCmdPkt)) {
              std::memcpy(&last_cmd, payload.data(), sizeof(last_cmd));
              have_cmd = true;
              last_cmd_time = clock::now();
              logger::debug() << "[TCP_GW] got CMD seq=" << last_cmd.seq;
            }
            // ignore other frames
          }
        }
      }
    }

    // ---- command validity + timeout safety ----
    const auto now = clock::now();
    const double cmd_age = std::chrono::duration<double>(now - last_cmd_time).count();
    const bool cmd_valid = have_cmd && (cmd_age <= config.cmd_timeout_s);

    const bool do_print = print.check();

    if (cmd_valid) {
      bot.apply_motor_cmd(last_cmd.motors);

      if (!last_cmd_valid)
        logger::info() << "[TCP_GW] CMD valid.\n";
      last_cmd_valid = true;

      if (do_print) {
        logger::info()
            << "[TCP_GW] CMD " << helpper::to_string(last_cmd) << "\n";
        commands_recorder.record_motor_cmd(utils::now(), last_cmd.motors);
      }
    } else {
      if (last_cmd_valid) {
        logger::warn() << "[TCP_GW] CMD timeout -> motors stop.\n";
        bot.set_motor(0, 0, 0, 0);
        last_cmd_valid = false;
      }
    }

    // ---- publish framed STATE (broadcast) ----
    const core::States s = bot.get_state();
    const uint32_t seq = ++state_seq;
    const float t_mono_s = std::chrono::duration<float>(now - t0).count();
    const connection::StatesPkt pkt = connection::state_to_state_pkt(seq, t_mono_s, s);
    const connection::MsgHdr h = connection::make_hdr(connection::MSG_STATE,
                                                       static_cast<uint8_t>(sizeof(pkt)));

    for (size_t i = 0; i < state_clients.size(); )
    {
      auto &c = state_clients[i];
      if (!c.is_open())
      {
        state_clients.erase(state_clients.begin() + i);
        continue;
      }

      if (!c.send_all(&h, sizeof(h)) || !c.send_all(&pkt, sizeof(pkt)))
      {
        c.close();
        logger::warn() << "[TCP_GW] STATE client disconnected (send failed).\n";
        state_clients.erase(state_clients.begin() + i);
        continue;
      }
      ++i;
    }

    if (do_print) {
      states_recorder.record_state(utils::now(), s);
    }

    // ---- fixed-rate schedule ----
    next += std::chrono::duration_cast<clock::duration>(dt);
    std::this_thread::sleep_until(next);
  }

  bot.set_motor(0, 0, 0, 0);
  logger::info() << "[TCP_GW] Exiting.\n";
  return 0;
}
