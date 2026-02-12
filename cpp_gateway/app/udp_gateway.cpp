
#include "connection/packets.hpp"
#include "connection/udp_socket.hpp"
#include "rosmaster/rosmaster.hpp"
#include "utils/logger.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

constexpr int SERIAL_BAUD{115200};
constexpr uint16_t DST_PORT{20001};
constexpr uint16_t CMD_PORT{20002};
constexpr double CMD_TIMEOUT{10.0}; // if no cmd received, stop motors
constexpr int STATE_PUBLISH_FREQ{1};
constexpr const char *SERIAL_DEV{"/dev/ttyUSB0"};
constexpr const char *DST_IP{"192.168.68.111"};
constexpr const char *LOCAL_IP{"0.0.0.0"};

static std::atomic<bool> g_run{true};

static void on_sigint(int) { g_run.store(false); }

using namespace std::chrono_literals;

struct Config
{
  std::string serial_dev{SERIAL_DEV};
  int serial_baud{SERIAL_BAUD};
  std::string local_ip{LOCAL_IP};
  std::string dst_ip{DST_IP};
  uint16_t dst_port{DST_PORT};
  uint16_t cmd_port{CMD_PORT};
  double hz{STATE_PUBLISH_FREQ};
  double cmd_timeout_s{CMD_TIMEOUT};
};

bool parse_config(int argc, char **argv, Config& config) {
  // Simple CLI:
  //  --serial /dev/ttyUSB0 --baud 115200
  //  --dst_ip 127.0.0.1 --state_port 25001
  //  --bind_ip 0.0.0.0 --cmd_port 25002
  //  --hz 200 --cmd_timeout 0.2
  for (int i = 1; i < argc; i++) {
    std::string_view a = argv[i];
    auto need = [&](std::string_view name) -> std::string_view {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--serial")
      config.serial_dev = std::string(need("--serial"));
    else if (a == "--baud")
      config.serial_baud = std::stoi(std::string(need("--baud")));
    else if (a == "--dst_ip")
      config.dst_ip = std::string(need("--dst_ip"));
    else if (a == "--state_port")
      config.dst_port = static_cast<uint16_t>(std::stoi(std::string(need("--state_port"))));
    else if (a == "--bind_ip")
      config.local_ip = std::string(need("--bind_ip"));
    else if (a == "--cmd_port")
      config.cmd_port = static_cast<uint16_t>(std::stoi(std::string(need("--cmd_port"))));
    else if (a == "--hz")
      config.hz = std::stod(std::string(need("--hz")));
    else if (a == "--cmd_timeout")
      config.cmd_timeout_s = std::stod(std::string(need("--cmd_timeout")));
    else if (a == "--help") {
      logger::info() << "Usage: " << argv[0] << " [options]\n"
                                                "  --serial /dev/ttyUSB0   Serial device\n"
                                                "  --baud 115200           Serial baud\n"
                                                "  --dst_ip 127.0.0.1      Where to send STATE UDP\n"
                                                "  --state_port 25001      Destination STATE UDP port\n"
                                                "  --bind_ip 0.0.0.0       Local bind IP for CMD UDP\n"
                                                "  --cmd_port 25002        Local CMD UDP port (controller sends here)\n"
                                                "  --hz 200                connection publish/apply rate\n"
                                                "  --cmd_timeout 0.2       Seconds before safety stop if no cmd\n";
      return EXIT_FAILURE;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
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

  // ---- Rosmaster ----
  rosmaster::Rosmaster bot;
  rosmaster::Config cfg;
  cfg.device = config.serial_dev;
  cfg.baud = config.serial_baud;
  cfg.debug = false;

  if (!bot.connect(cfg)) {
    logger::error() << "[GW] Failed to connect to " << cfg.device << "\n";
    return 1;
  }
  bot.start();
  bot.set_auto_report_state(true, false);

  // ---- UDP sockets ----
  connection::UdpSocket state_tx;
  if (!state_tx.set_tx_destination(config.dst_ip, config.dst_port)) {
    logger::error() << "[GW] Failed to set STATE destination " << config.dst_ip << ":" << config.dst_port << "\n";
    return 1;
  }

  connection::UdpSocket cmd_rx;
  if (!cmd_rx.bind_rx(config.local_ip, config.cmd_port, /*nonblocking=*/true)) {
    logger::error() << "[GW] Failed to bind CMD RX on " << config.local_ip << ":" << config.cmd_port << "\n";
    return 1;
  }

  logger::info() << "[GW] Serial=" << config.serial_dev << "@" << config.serial_baud
                 << " | STATE-> " << config.dst_ip << ":" << config.dst_port
                 << " | CMD<- " << config.local_ip << ":" << config.cmd_port
                 << " | rate=" << config.hz << " Hz\n";

  using clock = std::chrono::steady_clock;
  const auto dt = std::chrono::duration<double>(1.0 / config.hz);
  const auto t0 = clock::now();
  auto next = clock::now();

  connection::CmdPkt last_cmd{};
  bool have_cmd = false;
  auto last_cmd_time = clock::now();

  uint32_t state_seq = 0;

  while (g_run.load()) {
    // ---- receive latest CMD (non-blocking) ----
    for (;;) {
      connection::CmdPkt c_net{};
      size_t n = 0;
      if (!cmd_rx.try_recv(&c_net, sizeof(c_net), n))
        break;
      if (n == sizeof(c_net)) {
        last_cmd = c_net;
        have_cmd = true;
        last_cmd_time = clock::now();
      }
    }

    // ---- safety timeout ----
    const double cmd_age = std::chrono::duration<double>(clock::now() - last_cmd_time).count();
    const bool cmd_valid = have_cmd && (cmd_age <= config.cmd_timeout_s);

    // ---- apply command to board ----
    if (cmd_valid) {
      bot.apply_actions(last_cmd.actions);
      last_cmd.actions.beep_ms = 0;
    }
    // ---- publish state ----
    const core::States s = bot.get_state();
    uint32_t seq = ++state_seq;
    double t_mono_s = std::chrono::duration<double>(clock::now() - t0).count();
    connection::StatesPkt pkt = connection::state_to_state_pkt(seq, static_cast<float>(t_mono_s), s);
    (void)state_tx.send(&pkt, sizeof(pkt));

    // ---- fixed-rate schedule ----
    next += std::chrono::duration_cast<clock::duration>(dt);
    std::this_thread::sleep_until(next);
  }

  // safety stop on exit
  bot.set_motor(0, 0, 0, 0);
  logger::info() << "[GW] Exiting.\n";
  return 0;
}
