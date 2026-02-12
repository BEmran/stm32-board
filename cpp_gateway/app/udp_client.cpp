#include "connection/packets.hpp"
#include "connection/udp_socket.hpp"
#include "utils/logger.hpp"
#include "helpper.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

constexpr const char *DEFAULT_BIND_IP{"0.0.0.0"};
constexpr uint16_t DEFAULT_STATE_PORT{20001};

static std::atomic<bool> g_run{true};

static void on_sigint(int) { g_run.store(false); }

using namespace std::chrono_literals;

struct Config {
  std::string bind_ip{DEFAULT_BIND_IP};
  uint16_t state_port{DEFAULT_STATE_PORT};
  double print_hz{1.0};
};

bool parse_config(int argc, char **argv, Config &config) {
  // Simple CLI:
  //  --bind_ip 0.0.0.0 --state_port 20001 --print_hz 1
  for (int i = 1; i < argc; i++) {
    std::string_view a = argv[i];
    auto need = [&](std::string_view name) -> std::string_view {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--bind_ip") {
      config.bind_ip = std::string(need("--bind_ip"));
    } else if (a == "--state_port") {
      config.state_port = static_cast<uint16_t>(std::stoi(std::string(need("--state_port"))));
    } else if (a == "--print_hz") {
      config.print_hz = std::stod(std::string(need("--print_hz")));
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

helpper::Print print(1.0);
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
    connection::StatesPkt pkt{};
    size_t n = 0;
    if (!rx.try_recv(&pkt, sizeof(pkt), n)) {
      std::this_thread::sleep_for(1ms);
      continue;
    }
    if (n != sizeof(pkt)) continue;
    if (min_dt <= 0.0) continue;

    if (print.check()) {
      logger::info() << "[UDP] STATE " << helpper::to_string(pkt) << "\n";
    }
  }

  logger::info() << "[UDP] Exiting.\n";
  return 0;
}
