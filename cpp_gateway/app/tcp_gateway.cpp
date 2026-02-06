#include "connection/tcp_socket.hpp"
#include "connection/packets.hpp"
#include "connection/framed.hpp"
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
constexpr uint16_t PORT{30001};
constexpr double CMD_TIMEOUT{0.2};     // if no cmd received, stop motors
constexpr int STATE_PUBLISH_FREQ{200}; // Hz
constexpr const char *SERIAL_DEV{"/dev/ttyUSB0"};
constexpr const char *BIND_IP{"0.0.0.0"};

static std::atomic<bool> g_run{true};
static void on_sigint(int) { g_run.store(false); }

struct Config {
  std::string serial_dev{SERIAL_DEV};
  int serial_baud{SERIAL_BAUD};
  std::string bind_ip{BIND_IP};
  uint16_t port{PORT};
  double hz{STATE_PUBLISH_FREQ};
  double cmd_timeout_s{CMD_TIMEOUT};
};

bool parse_config(int argc, char **argv, Config &config)
{
  // Simple CLI:
  //  --serial /dev/ttyUSB0 --baud 115200
  //  --bind_ip 0.0.0.0 --port 30001
  //  --hz 200 --cmd_timeout 0.2
  //
  // Back-compat:
  //  --state_port is treated as --port (cmd_port is ignored)
  for (int i = 1; i < argc; i++)
  {
    std::string a = argv[i];
    auto need = [&](const char *name) -> std::string
    {
      if (i + 1 >= argc)
      {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return std::string(argv[++i]);
    };

    if (a == "--serial")      config.serial_dev = need("--serial");
    else if (a == "--baud")   config.serial_baud = std::stoi(need("--baud"));
    else if (a == "--bind_ip")config.bind_ip = need("--bind_ip");
    else if (a == "--port")   config.port = (uint16_t)std::stoi(need("--port"));
    else if (a == "--state_port") config.port = (uint16_t)std::stoi(need("--state_port"));
    else if (a == "--cmd_port")   { (void)need("--cmd_port"); /*ignored*/ }
    else if (a == "--hz")     config.hz = std::stod(need("--hz"));
    else if (a == "--cmd_timeout") config.cmd_timeout_s = std::stod(need("--cmd_timeout"));
    else if (a == "--help")
    {
      logger::info()
          << "Usage: " << argv[0] << " [options]\n"
          << "  --serial /dev/ttyUSB0   Serial device\n"
          << "  --baud 115200           Serial baud\n"
          << "  --bind_ip 0.0.0.0       Local bind IP\n"
          << "  --port 30001            TCP port (single connection for STATE+CMD)\n"
          << "  --hz 200                STATE publish rate\n"
          << "  --cmd_timeout 0.2       Command timeout (seconds)\n";
      return false;
    }
  }
  if (config.hz <= 0.0) config.hz = STATE_PUBLISH_FREQ;
  if (config.cmd_timeout_s <= 0.0) config.cmd_timeout_s = CMD_TIMEOUT;
  return true;
}

int main(int argc, char **argv)
{
  Config config;
  if (!parse_config(argc, argv, config))
    return 0;

  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif

  // ---- Rosmaster ----
  rosmaster::Rosmaster bot;
  rosmaster::Config cfg;
  cfg.device = config.serial_dev;
  cfg.baud = config.serial_baud;
  cfg.debug = false;

  if (!bot.connect(cfg))
  {
    logger::error() << "[TCP_GW] Failed to connect to " << cfg.device << "\n";
    return 1;
  }
  bot.start();
  bot.set_auto_report_state(true, false);

  // ---- TCP server (single client) ----
  connection::TcpSocket srv;
  if (!srv.bind_listen(config.bind_ip, config.port, /*backlog=*/1))
  {
    logger::error() << "[TCP_GW] Failed to bind on " << config.bind_ip
                    << ":" << config.port << "\n";
    return 1;
  }
  srv.set_nonblocking(true);

  logger::info() << "[TCP_GW] Serial=" << config.serial_dev << "@" << config.serial_baud
                 << " | TCP: " << config.bind_ip << ":" << config.port
                 << " | rate=" << config.hz << " Hz\n";

  connection::TcpSocket client;
  connection::FrameRx frx;

  using clock = std::chrono::steady_clock;
  const auto dt = std::chrono::duration<double>(1.0 / config.hz);
  const auto t0 = clock::now();
  auto next = clock::now();

  connection::CmdPktV1 last_cmd{};
  bool have_cmd = false;
  bool last_cmd_valid = false;
  auto last_cmd_time = clock::now();
  uint32_t state_seq = 0;

  while (g_run.load())
  {
    // ---- accept client ----
    if (!client.is_open())
    {
      if (srv.accept_client(client, /*nonblocking=*/true))
      {
        logger::info() << "[TCP_GW] client connected.\n";
        frx.clear();
        have_cmd = false;
        last_cmd_valid = false;
        last_cmd_time = clock::now();
      }
    }

    // ---- receive framed CMD messages (non-blocking) ----
    if (client.is_open())
    {
      uint8_t tmp[1024];
      size_t n = 0;
      if (client.try_recv(tmp, sizeof(tmp), n))
      {
        if (n == 0)
        {
          client.close();
          logger::warn() << "[TCP_GW] client disconnected.\n";
        }
        else
        {
          frx.push_bytes(tmp, n);

          uint8_t type = 0;
          std::vector<uint8_t> payload;
          while (frx.pop(type, payload))
          {
            if (type == connection::MSG_CMD && payload.size() == sizeof(connection::CmdPktV1))
            {
              connection::CmdPktV1 c{};
              std::memcpy(&c, payload.data(), sizeof(c));
              last_cmd = c;
              have_cmd = true;
              last_cmd_time = clock::now();
            }
            // ignore other frames
          }
        }
      }
    }

    // ---- command validity + timeout safety ----
    const double cmd_age = std::chrono::duration<double>(clock::now() - last_cmd_time).count();
    const bool cmd_valid = have_cmd && (cmd_age <= config.cmd_timeout_s);

    if (cmd_valid)
    {
      core::Actions actions = connection::cmd_pktv1_to_actions(last_cmd);
      logger::info() << "seq: " << last_cmd.seq << " m1: " << actions.motors.m1 << " m2: " << actions.motors.m2 << " m3: " << actions.motors.m3 << " m4: " << actions.motors.m4
      << " beep: " << actions.beep_ms << " flag: " << actions.flags;
      bot.apply_actions(actions);
      if (!last_cmd_valid)
        logger::info() << "[TCP_GW] CMD valid.\n";
      last_cmd_valid = true;
    }
    else
    {
      if (last_cmd_valid)
        logger::warn() << "[TCP_GW] CMD timeout -> motors stop.\n";
      bot.set_motor(0, 0, 0, 0);
      last_cmd_valid = false;
    }

    // ---- publish framed STATE ----
    const core::State s = bot.get_state();
    const uint32_t seq = ++state_seq;
    const float t_mono_s = std::chrono::duration<float>(clock::now() - t0).count();
    const connection::StatePktV1 pkt = connection::state_to_state_pktv1(seq, t_mono_s, s);

    if (client.is_open())
    {
      const connection::MsgHdr h = connection::make_hdr(connection::MSG_STATE, (uint16_t)sizeof(pkt));
      if (!client.send_all(&h, sizeof(h)) || !client.send_all(&pkt, sizeof(pkt)))
      {
        client.close();
        logger::warn() << "[TCP_GW] client disconnected (send failed).\n";
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
