#include "connection/packets.hpp"
#include "connection/udp_socket.hpp"
#include "utils/logger.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

constexpr const char *DEFAULT_BIND_IP{"0.0.0.0"};
constexpr uint16_t DEFAULT_STATE_PORT{20001};

static std::atomic<bool> g_run{true};

static void on_sigint(int) { g_run.store(false); }

struct Config {
  std::string bind_ip{DEFAULT_BIND_IP};
  uint16_t state_port{DEFAULT_STATE_PORT};
  double print_hz{1.0};
};

bool parse_config(int argc, char **argv, Config &config) {
  // Simple CLI:
  //  --bind_ip 0.0.0.0 --state_port 20001 --print_hz 1
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return std::string(argv[++i]);
    };
    if (a == "--bind_ip") {
      config.bind_ip = need("--bind_ip");
    } else if (a == "--state_port") {
      config.state_port = (uint16_t)std::stoi(need("--state_port"));
    } else if (a == "--print_hz") {
      config.print_hz = std::stod(need("--print_hz"));
    } else if (a == "--help") {
      logger::info() << "Usage: " << argv[0] << " [options]\n"
                                        "  --bind_ip 0.0.0.0       Local bind IP\n"
                                        "  --state_port 20001      Local STATE UDP port\n"
                                        "  --print_hz 1            Print rate (Hz, 0=off)\n";
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

  connection::UdpSocket rx;
  if (!rx.bind_rx(config.bind_ip, config.state_port, /*nonblocking=*/true)) {
    logger::error() << "[UDP] Failed to bind " << config.bind_ip << ":" << config.state_port << "\n";
    return 1;
  }

  logger::info() << "[UDP] Listening on " << config.bind_ip << ":" << config.state_port
                 << " print_hz=" << config.print_hz << "\n";

  using clock = std::chrono::steady_clock;
  const double min_dt = (config.print_hz > 0.0) ? (1.0 / config.print_hz) : 0.0;
  auto last_print = clock::now() - std::chrono::duration<double>(min_dt);

  while (g_run.load()) {
    connection::StatePktV1 pkt{};
    size_t n = 0;
    if (!rx.try_recv(&pkt, sizeof(pkt), n)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (n != sizeof(pkt)) continue;
    if (min_dt <= 0.0) continue;

    const auto now = clock::now();
    if (std::chrono::duration<double>(now - last_print).count() < min_dt) {
      continue;
    }
    last_print = now;

    logger::info() << "[UDP] STATE seq=" << pkt.seq
                   << " t_mono=" << pkt.t_mono_s
                   << " roll=" << pkt.roll
                   << " pitch=" << pkt.pitch
                   << " yaw=" << pkt.yaw
                   << " enc1=" << pkt.e1
                   << " enc2=" << pkt.e2
                   << " enc3=" << pkt.e3
                   << " enc4=" << pkt.e4
                   << " batt=" << pkt.battery_voltage << "\n";
  }

  logger::info() << "[UDP] Exiting.\n";
  return 0;
}
