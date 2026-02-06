#include "connection/tcp_socket.hpp"
#include "connection/udp_packets.hpp"
#include "utils/logger.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

constexpr const char *DEFAULT_SERVER_IP{"127.0.0.1"};
constexpr uint16_t DEFAULT_SERVER_PORT{20001};

static std::atomic<bool> g_run{true};

static void on_sigint(int) { g_run.store(false); }

struct Config {
  std::string server_ip{DEFAULT_SERVER_IP};
  uint16_t server_port{DEFAULT_SERVER_PORT};
  double print_hz{1.0};
};

bool parse_config(int argc, char **argv, Config &config) {
  // Simple CLI:
  //  --server_ip 127.0.0.1 --server_port 20001 --print_hz 1
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return std::string(argv[++i]);
    };
    if (a == "--server_ip") {
      config.server_ip = need("--server_ip");
    } else if (a == "--server_port") {
      config.server_port = (uint16_t)std::stoi(need("--server_port"));
    } else if (a == "--print_hz") {
      config.print_hz = std::stod(need("--print_hz"));
    } else if (a == "--help") {
      logger::info() << "Usage: " << argv[0] << " [options]\n"
                                        "  --server_ip 127.0.0.1    TCP server IP\n"
                                        "  --server_port 20001      TCP server port\n"
                                        "  --print_hz 1             Print rate (Hz, 0=off)\n";
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

  connection::TcpSocket sock;
  if (!sock.connect_to(config.server_ip, config.server_port, /*nonblocking=*/false)) {
    logger::error() << "[TCP] Failed to connect to " << config.server_ip << ":" << config.server_port << "\n";
    return 1;
  }

  logger::info() << "[TCP] Connected to " << config.server_ip << ":" << config.server_port
                 << " print_hz=" << config.print_hz << "\n";

  using clock = std::chrono::steady_clock;
  const double min_dt = (config.print_hz > 0.0) ? (1.0 / config.print_hz) : 0.0;
  auto last_print = clock::now() - std::chrono::duration<double>(min_dt);

  while (g_run.load()) {
    connection::StatePktV1 pkt{};
    if (!sock.recv_all(&pkt, sizeof(pkt))) {
      logger::warn() << "[TCP] Connection closed.\n";
      break;
    }
    if (min_dt <= 0.0) continue;

    const auto now = clock::now();
    if (std::chrono::duration<double>(now - last_print).count() < min_dt) continue;
    last_print = now;

    logger::info() << "[TCP] STATE seq=" << pkt.seq
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

  logger::info() << "[TCP] Exiting.\n";
  return 0;
}
