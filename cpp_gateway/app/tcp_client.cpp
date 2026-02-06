#include "connection/tcp_socket.hpp"
#include "connection/packets.hpp"
#include "connection/framed.hpp"
#include "utils/logger.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

constexpr const char *DEFAULT_SERVER_IP{"127.0.0.1"};
constexpr uint16_t DEFAULT_SERVER_PORT{30001};

static std::atomic<bool> g_run{true};
static void on_sigint(int) { g_run.store(false); }

struct Config {
  std::string server_ip{DEFAULT_SERVER_IP};
  uint16_t server_port{DEFAULT_SERVER_PORT};
  double print_hz{10.0};
};

bool parse_config(int argc, char **argv, Config &config) {
  //  --server_ip 127.0.0.1 --server_port 30001 --print_hz 10
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return std::string(argv[++i]);
    };

    if (a == "--server_ip") config.server_ip = need("--server_ip");
    else if (a == "--server_port") config.server_port = (uint16_t)std::stoi(need("--server_port"));
    else if (a == "--print_hz") config.print_hz = std::stod(need("--print_hz"));
    else if (a == "--help") {
      logger::info() << "Usage: " << argv[0] << " [options]\n"
                     << "  --server_ip 127.0.0.1    TCP server IP\n"
                     << "  --server_port 30001      TCP server port\n"
                     << "  --print_hz 10            Print rate (Hz, 0=off)\n";
      return false;
    } else {
      logger::error() << "Unknown arg: " << a << "\n";
      return false;
    }
  }
  return true;
}

int main(int argc, char *argv[]) {
  Config config;
  if (!parse_config(argc, argv, config)) return 0;

  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);

  connection::TcpSocket sock;
  if (!sock.connect_to(config.server_ip, config.server_port, /*nonblocking=*/false)) {
    logger::error() << "[TCP] Failed to connect to " << config.server_ip << ":" << config.server_port << "\n";
    return 1;
  }

  logger::info() << "[TCP] Connected to " << config.server_ip << ":" << config.server_port
                 << " print_hz=" << config.print_hz << "\n";

  connection::FrameRx frx;

  using clock = std::chrono::steady_clock;
  const double min_dt = (config.print_hz > 0.0) ? (1.0 / config.print_hz) : 0.0;
  auto last_print = clock::now() - std::chrono::duration<double>(min_dt);

  while (g_run.load()) {
    uint8_t tmp[1024];
    size_t n = 0;

    // blocking receive using recv_all is not valid with framing; we use try_recv in a loop
    if (sock.try_recv(tmp, sizeof(tmp), n)) {
      if (n == 0) {
        logger::warn() << "[TCP] Connection closed.\n";
        break;
      }
      frx.push_bytes(tmp, n);

      uint8_t type = 0;
      std::vector<uint8_t> payload;
      while (frx.pop(type, payload)) {
        if (type != connection::MSG_STATE) continue;
        if (payload.size() != sizeof(connection::StatePktV1)) continue;

        connection::StatePktV1 pkt{};
        std::memcpy(&pkt, payload.data(), sizeof(pkt));

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
    } else {
      // no data (or EAGAIN), sleep a bit
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  return 0;
}
