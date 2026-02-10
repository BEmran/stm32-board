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
constexpr uint16_t DEFAULT_STATE_PORT{30001};
constexpr uint16_t DEFAULT_CMD_PORT{30002};

static std::atomic<bool> g_run{true};
static void on_sigint(int) { g_run.store(false); }

struct Config {
  std::string server_ip{DEFAULT_SERVER_IP};

  uint16_t state_port{DEFAULT_STATE_PORT};
  uint16_t cmd_port{DEFAULT_CMD_PORT};

  double print_hz{10.0}; // STATE print rate (Hz, 0=off)
  double cmd_hz{50.0};   // CMD send rate (Hz, 0=off)

  int m1{0}, m2{0}, m3{0}, m4{0}; // motor command values (example units)
  int beep_ms{0};
  uint32_t flags{0};
};

static bool parse_config(int argc, char **argv, Config &config)
{
  // Examples:
  //  --server_ip 192.168.1.10 --state_port 30001 --cmd_port 30002
  //  --print_hz 10 --cmd_hz 50 --m1 20 --m2 20 --m3 20 --m4 20
  for (int i = 1; i < argc; i++)
  {
    std::string a = argv[i];
    auto need = [&](const char *name) -> std::string {
      if (i + 1 >= argc)
      {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return std::string(argv[++i]);
    };

    if (a == "--server_ip") config.server_ip = need("--server_ip");
    else if (a == "--state_port") config.state_port = (uint16_t)std::stoi(need("--state_port"));
    else if (a == "--cmd_port") config.cmd_port = (uint16_t)std::stoi(need("--cmd_port"));

    else if (a == "--print_hz") config.print_hz = std::stod(need("--print_hz"));
    else if (a == "--cmd_hz") config.cmd_hz = std::stod(need("--cmd_hz"));

    else if (a == "--m1") config.m1 = std::stoi(need("--m1"));
    else if (a == "--m2") config.m2 = std::stoi(need("--m2"));
    else if (a == "--m3") config.m3 = std::stoi(need("--m3"));
    else if (a == "--m4") config.m4 = std::stoi(need("--m4"));

    else if (a == "--beep_ms") config.beep_ms = std::stoi(need("--beep_ms"));
    else if (a == "--flags") config.flags = (uint32_t)std::stoul(need("--flags"));

    else if (a == "--help")
    {
      logger::info()
          << "Usage: " << argv[0] << " [options]\n"
          << "  --server_ip 127.0.0.1     Gateway IP\n"
          << "  --state_port 30001        STATE port (recv)\n"
          << "  --cmd_port 30002          CMD port (send)\n"
          << "  --print_hz 10             Print STATE rate (Hz, 0=off)\n"
          << "  --cmd_hz 50               Send CMD rate (Hz, 0=off)\n"
          << "  --m1 0 --m2 0 --m3 0 --m4 0   Motor command values\n"
          << "  --beep_ms 0               Beep duration\n"
          << "  --flags 0                 Flags bitfield\n";
      return false;
    }
    else
    {
      logger::error() << "Unknown arg: " << a << "\n";
      return false;
    }
  }
  return true;
}

int main(int argc, char *argv[])
{
  Config config;
  if (!parse_config(argc, argv, config)) return 0;

  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);

  // --- connect STATE socket ---
  connection::TcpSocket state_sock;
  if (!state_sock.connect_to(config.server_ip, config.state_port, /*nonblocking=*/false))
  {
    logger::error() << "[TCP_CLIENT] Failed to connect STATE to " << config.server_ip << ":" << config.state_port << "\n";
    return 1;
  }

  // --- connect CMD socket ---
  connection::TcpSocket cmd_sock;
  if (!cmd_sock.connect_to(config.server_ip, config.cmd_port, /*nonblocking=*/false))
  {
    logger::error() << "[TCP_CLIENT] Failed to connect CMD to " << config.server_ip << ":" << config.cmd_port << "\n";
    return 1;
  }

  logger::info()
      << "[TCP_CLIENT] Connected. STATE=" << config.server_ip << ":" << config.state_port
      << " CMD=" << config.server_ip << ":" << config.cmd_port
      << " print_hz=" << config.print_hz
      << " cmd_hz=" << config.cmd_hz
      << " motors=(" << config.m1 << "," << config.m2 << "," << config.m3 << "," << config.m4 << ")\n";

  // --- CMD sender thread ---
  std::thread cmd_thread([&](){
    using clock = std::chrono::steady_clock;

    if (config.cmd_hz <= 0.0) return;

    const auto dt = std::chrono::duration<double>(1.0 / config.cmd_hz);
    auto next = clock::now();

    uint32_t seq = 0;

    while (g_run.load())
    {
      connection::CmdPktV1 cmd{};
      cmd.seq = ++seq;

      // NOTE: these fields must match your CmdPktV1 definition
      cmd.m1 = (int16_t)config.m1;
      cmd.m2 = (int16_t)config.m2;
      cmd.m3 = (int16_t)config.m3;
      cmd.m4 = (int16_t)config.m4;

      cmd.beep_ms = (uint16_t)config.beep_ms;
      cmd.flags = config.flags;

      const connection::CmdPktV1 cmd_net = connection::cmd_pktv1_host_to_net(cmd);
      const connection::MsgHdr h = connection::make_hdr(connection::MSG_CMD, (uint16_t)sizeof(cmd_net));

      if (!cmd_sock.send_all(&h, sizeof(h)) || !cmd_sock.send_all(&cmd_net, sizeof(cmd_net)))
      {
        logger::warn() << "[TCP_CLIENT] CMD send failed -> disconnect.\n";
        break;
      }

      next += std::chrono::duration_cast<clock::duration>(dt);
      std::this_thread::sleep_until(next);
    }
  });

  // --- STATE receiver loop ---
  connection::FrameRx frx;

  using clock = std::chrono::steady_clock;
  const double min_dt = (config.print_hz > 0.0) ? (1.0 / config.print_hz) : 0.0;
  auto last_print = clock::now() - std::chrono::duration<double>(min_dt);

  while (g_run.load())
  {
    uint8_t tmp[1024];
    size_t n = 0;

    if (state_sock.try_recv(tmp, sizeof(tmp), n))
    {
      if (n == 0)
      {
        logger::warn() << "[TCP_CLIENT] STATE connection closed.\n";
        break;
      }

      frx.push_bytes(tmp, n);

      uint8_t type = 0;
      std::vector<uint8_t> payload;
      while (frx.pop(type, payload))
      {
        if (type != connection::MSG_STATE) continue;
        if (payload.size() != sizeof(connection::StatePktV1)) continue;

        connection::StatePktV1 pkt_net{};
        std::memcpy(&pkt_net, payload.data(), sizeof(pkt_net));
        const connection::StatePktV1 pkt = connection::state_pktv1_net_to_host(pkt_net);

        if (min_dt <= 0.0) continue;
        const auto now = clock::now();
        if (std::chrono::duration<double>(now - last_print).count() < min_dt) continue;
        last_print = now;

        logger::info() << "[TCP_CLIENT] STATE seq=" << pkt.seq
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
    }
    else
    {
      // no data (or EAGAIN), sleep a bit
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  g_run.store(false);
  if (cmd_thread.joinable()) cmd_thread.join();

  return 0;
}
