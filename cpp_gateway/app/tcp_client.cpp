#include "connection/tcp_socket.hpp"
#include "connection/packets.hpp"
#include "connection/wire_codec.hpp"
#include "connection/framed.hpp"
#include "utils/logger.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

constexpr const char *DEFAULT_SERVER_IP{"127.0.0.1"};
constexpr uint16_t DEFAULT_STATE_PORT{30001};
constexpr uint16_t DEFAULT_CMD_PORT{30002};

static std::atomic<bool> g_run{true};
static void on_sigint(int) { g_run.store(false); }

using namespace std::chrono_literals;

struct Config
{
  std::string server_ip{DEFAULT_SERVER_IP};
  uint16_t state_port{DEFAULT_STATE_PORT};
  uint16_t cmd_port{DEFAULT_CMD_PORT};

  // Printing
  double print_hz{10.0}; // STATE print rate (Hz, 0=off)

  // Legacy CMD sender (MSG_CMD)
  double cmd_hz{50.0}; // CMD send rate (Hz, 0=off)
  int m1{0}, m2{0}, m3{0}, m4{0};
  int beep_ms{0};   // 0..255 recommended
  uint32_t flags{0}; // low 8 bits used

  // Setpoint sender (MSG_SETPOINT)
  double setpoint_hz{0.0}; // 0=off
  float sp0{0.0f}, sp1{0.0f}, sp2{0.0f}, sp3{0.0f};
  uint32_t sp_flags{0}; // low 8 bits used

  // Config sender (MSG_CONFIG) one-shot
  bool send_config{false};
  uint8_t  cfg_key{0};
  uint8_t  cfg_u8{0};
  uint16_t cfg_u16{0};
  uint32_t cfg_u32{0};
};

static bool parse_u8(std::string_view s, uint8_t &out)
{
  unsigned v = 0;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    if (std::sscanf(std::string(s).c_str(), "%x", &v) == 1 && v <= 0xFF) { out = (uint8_t)v; return true; }
    return false;
  }
  if (std::sscanf(std::string(s).c_str(), "%u", &v) == 1 && v <= 0xFF) { out = (uint8_t)v; return true; }
  return false;
}

static bool parse_u16(std::string_view s, uint16_t &out)
{
  unsigned v = 0;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    if (std::sscanf(std::string(s).c_str(), "%x", &v) == 1 && v <= 0xFFFF) { out = (uint16_t)v; return true; }
    return false;
  }
  if (std::sscanf(std::string(s).c_str(), "%u", &v) == 1 && v <= 0xFFFF) { out = (uint16_t)v; return true; }
  return false;
}

static bool parse_u32(std::string_view s, uint32_t &out)
{
  unsigned long v = 0;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    if (std::sscanf(std::string(s).c_str(), "%lx", &v) == 1 && v <= 0xFFFFFFFFul) { out = (uint32_t)v; return true; }
    return false;
  }
  if (std::sscanf(std::string(s).c_str(), "%lu", &v) == 1 && v <= 0xFFFFFFFFul) { out = (uint32_t)v; return true; }
  return false;
}

static void print_help(const char *argv0)
{
  logger::info()
    << "Usage: " << argv0 << " [options]\n"
    << "  --server_ip 127.0.0.1\n"
    << "  --state_port 30001\n"
    << "  --cmd_port 30002\n"
    << "  --print_hz 10\n"
    << "\n"
    << "Legacy CMD (MSG_CMD):\n"
    << "  --cmd_hz 50\n"
    << "  --m1 0 --m2 0 --m3 0 --m4 0\n"
    << "  --beep_ms 0\n"
    << "  --flags 0x00\n"
    << "\n"
    << "Setpoint (MSG_SETPOINT):\n"
    << "  --setpoint_hz 0\n"
    << "  --sp0 0 --sp1 0 --sp2 0 --sp3 0\n"
    << "  --sp_flags 0x00\n"
    << "\n"
    << "Config one-shot (MSG_CONFIG):\n"
    << "  --send_config 0|1\n"
    << "  --cfg_key 10   (example)\n"
    << "  --cfg_u8 0x07  --cfg_u16 0 --cfg_u32 0\n";
}

static bool parse_config(int argc, char **argv, Config &config)
{
  for (int i = 1; i < argc; i++) {
    std::string_view a = argv[i];
    auto need = [&](std::string_view name) -> std::string_view {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };

    if (a == "--server_ip") config.server_ip = std::string(need(a));
    else if (a == "--state_port") config.state_port = static_cast<uint16_t>(std::stoi(std::string(need(a))));
    else if (a == "--cmd_port") config.cmd_port = static_cast<uint16_t>(std::stoi(std::string(need(a))));

    else if (a == "--print_hz") config.print_hz = std::stod(std::string(need(a)));

    // legacy cmd
    else if (a == "--cmd_hz") config.cmd_hz = std::stod(std::string(need(a)));
    else if (a == "--m1") config.m1 = std::stoi(std::string(need(a)));
    else if (a == "--m2") config.m2 = std::stoi(std::string(need(a)));
    else if (a == "--m3") config.m3 = std::stoi(std::string(need(a)));
    else if (a == "--m4") config.m4 = std::stoi(std::string(need(a)));
    else if (a == "--beep_ms") config.beep_ms = std::stoi(std::string(need(a)));
    else if (a == "--flags") {
      uint32_t v = 0;
      if (!parse_u32(need(a), v)) { logger::error() << "Invalid --flags\n"; return false; }
      config.flags = v;
    }

    // setpoint
    else if (a == "--setpoint_hz") config.setpoint_hz = std::stod(std::string(need(a)));
    else if (a == "--sp0") config.sp0 = std::stof(std::string(need(a)));
    else if (a == "--sp1") config.sp1 = std::stof(std::string(need(a)));
    else if (a == "--sp2") config.sp2 = std::stof(std::string(need(a)));
    else if (a == "--sp3") config.sp3 = std::stof(std::string(need(a)));
    else if (a == "--sp_flags") {
      uint32_t v = 0;
      if (!parse_u32(need(a), v)) { logger::error() << "Invalid --sp_flags\n"; return false; }
      config.sp_flags = v;
    }

    // config one-shot
    else if (a == "--send_config") config.send_config = (std::stoi(std::string(need(a))) != 0);
    else if (a == "--cfg_key") {
      uint8_t v = 0;
      if (!parse_u8(need(a), v)) { logger::error() << "Invalid --cfg_key\n"; return false; }
      config.cfg_key = v;
    }
    else if (a == "--cfg_u8") {
      uint8_t v = 0;
      if (!parse_u8(need(a), v)) { logger::error() << "Invalid --cfg_u8\n"; return false; }
      config.cfg_u8 = v;
    }
    else if (a == "--cfg_u16") {
      uint16_t v = 0;
      if (!parse_u16(need(a), v)) { logger::error() << "Invalid --cfg_u16\n"; return false; }
      config.cfg_u16 = v;
    }
    else if (a == "--cfg_u32") {
      uint32_t v = 0;
      if (!parse_u32(need(a), v)) { logger::error() << "Invalid --cfg_u32\n"; return false; }
      config.cfg_u32 = v;
    }

    else if (a == "--help") {
      print_help(argv[0]);
      return false;
    }
    else {
      logger::error() << "Unknown arg: " << a << "\n";
      print_help(argv[0]);
      return false;
    }
  }

  return true;
}

static bool send_frame(connection::TcpSocket &sock, uint8_t type, const void *payload, uint8_t payload_len)
{
  const connection::MsgHdr h = connection::make_hdr(type, payload_len);
  if (!sock.send_all(&h, sizeof(h))) return false;
  if (payload_len == 0) return true;
  return sock.send_all(payload, payload_len);
}

int main(int argc, char *argv[])
{
  Config config;
  if (!parse_config(argc, argv, config)) return 0;

  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif

  // Connect STATE socket
  connection::TcpSocket state_sock;
  if (!state_sock.connect_to(config.server_ip, config.state_port, /*nonblocking=*/false)) {
    logger::error() << "[TCP_CLIENT] Failed to connect STATE to "
                    << config.server_ip << ":" << config.state_port << "\n";
    return 1;
  }

  // Connect CMD socket (used for MSG_CMD + MSG_SETPOINT + MSG_CONFIG)
  connection::TcpSocket cmd_sock;
  if (!cmd_sock.connect_to(config.server_ip, config.cmd_port, /*nonblocking=*/false)) {
    logger::error() << "[TCP_CLIENT] Failed to connect CMD to "
                    << config.server_ip << ":" << config.cmd_port << "\n";
    return 1;
  }

  logger::info() << "[TCP_CLIENT] Connected. STATE=" << config.server_ip << ":" << config.state_port
                 << " CMD=" << config.server_ip << ":" << config.cmd_port
                 << " print_hz=" << config.print_hz
                 << " cmd_hz=" << config.cmd_hz
                 << " setpoint_hz=" << config.setpoint_hz
                 << " flags=0x" << std::hex << (config.flags & 0xFFu) << std::dec
                 << "\n";

  // Optional CONFIG one-shot at start
  if (config.send_config) {
    connection::wire::ConfigPayload cp{};
    cp.seq = 1;
    cp.key = config.cfg_key;
    cp.u8  = config.cfg_u8;
    cp.u16 = config.cfg_u16;
    cp.u32 = config.cfg_u32;

    std::array<uint8_t, connection::wire::kConfigPayloadSize> buf{};
    connection::wire::encode_config_payload(buf, cp);

    if (!send_frame(cmd_sock, connection::MSG_CONFIG, buf.data(), static_cast<uint8_t>(buf.size()))) {
      logger::warn() << "[TCP_CLIENT] CONFIG send failed.\n";
    } else {
      logger::info() << "[TCP_CLIENT] CONFIG sent: key=" << unsigned(cp.key)
                     << " u8=" << unsigned(cp.u8)
                     << " u16=" << cp.u16
                     << " u32=" << cp.u32 << "\n";
    }
  }

  // Legacy CMD sender thread (MSG_CMD)
  std::thread cmd_thread([&]() {
    using clock = std::chrono::steady_clock;
    if (config.cmd_hz <= 0.0) return;

    const auto dt = std::chrono::duration<double>(1.0 / config.cmd_hz);
    auto next = clock::now();

    uint32_t seq = 0;

    while (g_run.load()) {
      connection::wire::CmdPayload cmd{};
      cmd.seq = ++seq;

      cmd.actions.motors.m1 = static_cast<int16_t>(config.m1);
      cmd.actions.motors.m2 = static_cast<int16_t>(config.m2);
      cmd.actions.motors.m3 = static_cast<int16_t>(config.m3);
      cmd.actions.motors.m4 = static_cast<int16_t>(config.m4);

      const int beep_ms = std::clamp(config.beep_ms, 0, 255);
      const uint32_t flags = static_cast<uint32_t>(std::clamp(config.flags & 0xFFu, 0u, 255u));

      cmd.actions.beep_ms = static_cast<uint8_t>(beep_ms);
      cmd.actions.flags   = static_cast<uint8_t>(flags);

      std::array<uint8_t, connection::wire::kCmdPayloadSize> cbuf{};
      connection::wire::encode_cmd_payload(cbuf, cmd);
      if (!send_frame(cmd_sock, connection::MSG_CMD, cbuf.data(), static_cast<uint8_t>(cbuf.size()))) {
        logger::warn() << "[TCP_CLIENT] CMD send failed -> disconnect.\n";
        break;
      }

      next += std::chrono::duration_cast<clock::duration>(dt);
      std::this_thread::sleep_until(next);
    }
  });

  // Setpoint sender thread (MSG_SETPOINT)
  std::thread setpoint_thread([&]() {
    using clock = std::chrono::steady_clock;
    if (config.setpoint_hz <= 0.0) return;

    const auto dt = std::chrono::duration<double>(1.0 / config.setpoint_hz);
    auto next = clock::now();

    uint32_t seq = 0;

    while (g_run.load()) {
      connection::wire::SetpointPayload sp{};
      sp.seq = ++seq;
      sp.sp[0] = config.sp0;
      sp.sp[1] = config.sp1;
      sp.sp[2] = config.sp2;
      sp.sp[3] = config.sp3;

      const uint32_t flags = static_cast<uint32_t>(std::clamp(config.sp_flags & 0xFFu, 0u, 255u));
      sp.flags = static_cast<uint8_t>(flags);

      std::array<uint8_t, connection::wire::kSetpointPayloadSize> sbuf{};
      connection::wire::encode_setpoint_payload(sbuf, sp);
      if (!send_frame(cmd_sock, connection::MSG_SETPOINT, sbuf.data(), static_cast<uint8_t>(sbuf.size()))) {
        logger::warn() << "[TCP_CLIENT] SETPOINT send failed -> disconnect.\n";
        break;
      }

      next += std::chrono::duration_cast<clock::duration>(dt);
      std::this_thread::sleep_until(next);
    }
  });

  // STATE receiver loop
  connection::FrameRx frx;

  using clock = std::chrono::steady_clock;
  const double min_dt = (config.print_hz > 0.0) ? (1.0 / config.print_hz) : 0.0;
  auto last_print = clock::now() - std::chrono::duration<double>(min_dt);

  while (g_run.load()) {
    uint8_t tmp[1024];
    size_t n = 0;

    if (state_sock.try_recv(tmp, sizeof(tmp), n)) {
      if (n == 0) {
        logger::warn() << "[TCP_CLIENT] STATE connection closed.\n";
        break;
      }

      frx.push_bytes(tmp, n);

      uint8_t type = 0;
      std::vector<uint8_t> payload;

      while (frx.pop(type, payload)) {
        if (type != connection::MSG_STATE) continue;
        if (payload.size() != connection::wire::kStatesPayloadSize) continue;

        // Decode explicitly (portable).
        const uint8_t* p = payload.data();
        const uint32_t seq = connection::wire::read_u32_le(p + 0);
        const float t_mono = connection::wire::read_f32_le(p + 4);
        const int32_t e1 = connection::wire::read_i32_le(p + 56);
        const float batt = connection::wire::read_f32_le(p + 72);

        if (min_dt <= 0.0) continue;
        const auto now = clock::now();
        if (std::chrono::duration<double>(now - last_print).count() < min_dt) continue;
        last_print = now;

        logger::info() << "[TCP_CLIENT] STATE seq=" << seq
                       << " t_mono=" << t_mono
                       << " roll=" << connection::wire::read_f32_le(payload.data() + 44)
                       << " pitch=" << connection::wire::read_f32_le(payload.data() + 48)
                       << " yaw=" << connection::wire::read_f32_le(payload.data() + 52)
                       << " enc1=" << e1
                       << " enc2=" << connection::wire::read_i32_le(payload.data() + 60)
                       << " enc3=" << connection::wire::read_i32_le(payload.data() + 64)
                       << " enc4=" << connection::wire::read_i32_le(payload.data() + 68)
                       << " batt=" << batt
                       << "\n";
      }
    } else {
      std::this_thread::sleep_for(1ms);
    }
  }

  g_run.store(false);
  if (cmd_thread.joinable()) cmd_thread.join();
  if (setpoint_thread.joinable()) setpoint_thread.join();

  return 0;
}
