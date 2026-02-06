#include "connection/tcp_socket.hpp"
#include "connection/packets.hpp"
#include "rosmaster/rosmaster.hpp"
#include "utils/logger.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <vector>

constexpr int SERIAL_BAUD{115200};
constexpr uint16_t STATE_PORT{20001};
constexpr uint16_t CMD_PORT{20002};
constexpr double CMD_TIMEOUT{10.0}; // if no cmd received, stop motors
constexpr int STATE_PUBLISH_FREQ{1};
constexpr const char *SERIAL_DEV{"/dev/ttyUSB0"};
constexpr const char *BIND_IP{"0.0.0.0"};

static std::atomic<bool> g_run{true};

static void on_sigint(int) { g_run.store(false); }

struct Config {
  std::string serial_dev{SERIAL_DEV};
  int serial_baud{SERIAL_BAUD};
  std::string bind_ip{BIND_IP};
  uint16_t state_port{STATE_PORT};
  uint16_t cmd_port{CMD_PORT};
  double hz{STATE_PUBLISH_FREQ};
  double cmd_timeout_s{CMD_TIMEOUT};
};

bool parse_config(int argc, char **argv, Config& config) {
  // Simple CLI:
  //  --serial /dev/ttyUSB0 --baud 115200
  //  --bind_ip 0.0.0.0 --state_port 20001 --cmd_port 20002
  //  --hz 200 --cmd_timeout 0.2
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return std::string(argv[++i]);
    };
    if (a == "--serial")
      config.serial_dev = need("--serial");
    else if (a == "--baud")
      config.serial_baud = std::stoi(need("--baud"));
    else if (a == "--bind_ip")
      config.bind_ip = need("--bind_ip");
    else if (a == "--state_port")
      config.state_port = (uint16_t)std::stoi(need("--state_port"));
    else if (a == "--cmd_port")
      config.cmd_port = (uint16_t)std::stoi(need("--cmd_port"));
    else if (a == "--hz")
      config.hz = std::stoi(need("--hz"));
    else if (a == "--cmd_timeout")
      config.cmd_timeout_s = std::stod(need("--cmd_timeout"));
    else if (a == "--help") {
      logger::info() << "Usage: " << argv[0] << " [options]\n"
                                        "  --serial /dev/ttyUSB0   Serial device\n"
                                        "  --baud 115200           Serial baud\n"
                                        "  --bind_ip 0.0.0.0       Local bind IP\n"
                                        "  --state_port 20001      TCP STATE port\n"
                                        "  --cmd_port 20002        TCP CMD port\n"
                                        "  --hz 200                publish/apply rate\n"
                                        "  --cmd_timeout 0.2       Seconds before safety stop if no cmd\n";
      return EXIT_FAILURE;
    } else {
      logger::error() << "Unknown arg: " << a << "\n";
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
  Config config;
  if (parse_config(argc, argv, config) == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);
#ifdef SIGPIPE
  // Prevent SIGPIPE from terminating the process on send() to a closed socket.
  std::signal(SIGPIPE, SIG_IGN);
#endif

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

  // ---- TCP servers ----
  connection::TcpSocket state_srv;
  if (!state_srv.bind_listen(config.bind_ip, config.state_port, /*backlog=*/1)) {
    logger::error() << "[TCP_GW] Failed to bind STATE on " << config.bind_ip
                    << ":" << config.state_port << "\n";
    return 1;
  }
  state_srv.set_nonblocking(true);

  connection::TcpSocket cmd_srv;
  if (!cmd_srv.bind_listen(config.bind_ip, config.cmd_port, /*backlog=*/1)) {
    logger::error() << "[TCP_GW] Failed to bind CMD on " << config.bind_ip
                    << ":" << config.cmd_port << "\n";
    return 1;
  }
  cmd_srv.set_nonblocking(true);

  logger::info() << "[TCP_GW] Serial=" << config.serial_dev << "@" << config.serial_baud
                 << " | STATE: " << config.bind_ip << ":" << config.state_port
                 << " | CMD: " << config.bind_ip << ":" << config.cmd_port
                 << " | rate=" << config.hz << " Hz\n";

  connection::TcpSocket state_client;
  connection::TcpSocket cmd_client;
  std::vector<uint8_t> cmd_buf;
  cmd_buf.reserve(sizeof(connection::CmdPktV1) * 4);

  using clock = std::chrono::steady_clock;
  const auto dt = std::chrono::duration<double>(1.0 / config.hz);
  const auto t0 = clock::now();
  auto next = clock::now();

  connection::CmdPktV1 last_cmd{};
  bool have_cmd = false;
  auto last_cmd_time = clock::now();
  uint32_t state_seq = 0;

  while (g_run.load()) {
    // ---- accept clients (non-blocking) ----
    if (!state_client.is_open()) {
      if (state_srv.accept_client(state_client, /*nonblocking=*/true)) {
        logger::info() << "[TCP_GW] STATE client connected.\n";
      }
    }
    if (!cmd_client.is_open()) {
      if (cmd_srv.accept_client(cmd_client, /*nonblocking=*/true)) {
        logger::info() << "[TCP_GW] CMD client connected.\n";
        cmd_buf.clear();
      }
    }

    // ---- receive latest CMD (non-blocking, stream framing) ----
    if (cmd_client.is_open()) {
      uint8_t tmp[256];
      size_t n = 0;
      if (cmd_client.try_recv(tmp, sizeof(tmp), n)) {
        if (n == 0) {
          cmd_client.close();
          logger::warn() << "[TCP_GW] CMD client disconnected.\n";
        } else {
          cmd_buf.insert(cmd_buf.end(), tmp, tmp + n);
          while (cmd_buf.size() >= sizeof(connection::CmdPktV1)) {
            connection::CmdPktV1 c{};
            std::memcpy(&c, cmd_buf.data(), sizeof(c));
            cmd_buf.erase(cmd_buf.begin(), cmd_buf.begin() + sizeof(c));
            last_cmd = c;
            have_cmd = true;
            last_cmd_time = clock::now();
          }
        }
      }
    }

    // ---- safety timeout ----
    const double cmd_age = std::chrono::duration<double>(clock::now() - last_cmd_time).count();
    const bool cmd_valid = have_cmd && (cmd_age <= config.cmd_timeout_s);

    // ---- apply command to board ----
    core::Actions actions = connection::cmd_pktv1_to_actions(last_cmd);
    if (cmd_valid) {
      bot.apply_actions(actions);
      actions.beep_ms = 0;
    }

    // ---- publish state ----
    const core::State s = bot.get_state();
    uint32_t seq = ++state_seq;
    float t_mono_s = std::chrono::duration<float>(clock::now() - t0).count();
    connection::StatePktV1 pkt = connection::state_to_state_pktv1(seq, t_mono_s, s);

    if (state_client.is_open()) {
      if (!state_client.send_all(&pkt, sizeof(pkt))) {
        state_client.close();
        logger::warn() << "[TCP_GW] STATE client disconnected.\n";
      }
    }

    // ---- fixed-rate schedule ----
    next += std::chrono::duration_cast<clock::duration>(dt);
    std::this_thread::sleep_until(next);
  }

  bot.set_motor(0, 0, 0, 0);
  logger::info() << "[TCP_GW] Exiting.\n";
  return 0;
}
