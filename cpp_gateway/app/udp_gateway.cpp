
#include "connection/udp_packets.hpp"
#include "connection/udp_socket.hpp"
#include "rosmaster/rosmaster.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>

static std::atomic<bool> g_run{true};

static void on_sigint(int) { g_run.store(false); }

static int16_t clamp_i16(int v, int lo, int hi) {
  if (v < lo) return (int16_t)lo;
  if (v > hi) return (int16_t)hi;
  return (int16_t)v;
}

int main(int argc, char** argv) {
  // ---- Defaults (edit to match your network) ----
  std::string serial_dev = "/dev/ttyUSB0";
  int serial_baud = 115200;

  // Publish state to (dst_ip, dst_state_port)
  std::string dst_ip = "127.0.0.1";
  uint16_t dst_state_port = 25001;

  // Receive commands on cmd_port (bind local)
  std::string bind_ip = "0.0.0.0";
  uint16_t cmd_port = 25002;

  double hz = 200.0;
  double cmd_timeout_s = 0.2; // if no cmd received, stop motors

  // Simple CLI:
  //  --serial /dev/ttyUSB0 --baud 115200
  //  --dst_ip 127.0.0.1 --state_port 25001
  //  --bind_ip 0.0.0.0 --cmd_port 25002
  //  --hz 200 --cmd_timeout 0.2
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char* name) -> std::string {
      if (i + 1 >= argc) { std::cerr << "Missing value for " << name << "\n"; std::exit(2); }
      return std::string(argv[++i]);
    };
    if (a == "--serial") serial_dev = need("--serial");
    else if (a == "--baud") serial_baud = std::stoi(need("--baud"));
    else if (a == "--dst_ip") dst_ip = need("--dst_ip");
    else if (a == "--state_port") dst_state_port = (uint16_t)std::stoi(need("--state_port"));
    else if (a == "--bind_ip") bind_ip = need("--bind_ip");
    else if (a == "--cmd_port") cmd_port = (uint16_t)std::stoi(need("--cmd_port"));
    else if (a == "--hz") hz = std::stod(need("--hz"));
    else if (a == "--cmd_timeout") cmd_timeout_s = std::stod(need("--cmd_timeout"));
    else if (a == "--help") {
      std::cout <<
        "Usage: " << argv[0] << " [options]\n"
        "  --serial /dev/ttyUSB0   Serial device\n"
        "  --baud 115200           Serial baud\n"
        "  --dst_ip 127.0.0.1      Where to send STATE UDP\n"
        "  --state_port 25001      Destination STATE UDP port\n"
        "  --bind_ip 0.0.0.0       Local bind IP for CMD UDP\n"
        "  --cmd_port 25002        Local CMD UDP port (controller sends here)\n"
        "  --hz 200                Gateway publish/apply rate\n"
        "  --cmd_timeout 0.2       Seconds before safety stop if no cmd\n";
      return 0;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      return 2;
    }
  }

  std::signal(SIGINT, on_sigint);
  std::signal(SIGTERM, on_sigint);

  // ---- Rosmaster ----
  rosmaster::Rosmaster bot;
  rosmaster::Config cfg;
  cfg.device = serial_dev;
  cfg.baud = serial_baud;
  cfg.debug = false;

  if (!bot.connect(cfg)) {
    std::cerr << "[GW] Failed to connect to " << serial_dev << "\n";
    return 1;
  }
  bot.start();
  bot.set_auto_report_state(true, false);

  // ---- UDP sockets ----
  gateway::UdpSocket state_tx;
  if (!state_tx.set_tx_destination(dst_ip, dst_state_port)) {
    std::cerr << "[GW] Failed to set STATE destination " << dst_ip << ":" << dst_state_port << "\n";
    return 1;
  }

  gateway::UdpSocket cmd_rx;
  if (!cmd_rx.bind_rx(cmd_port, bind_ip, /*nonblocking=*/true)) {
    std::cerr << "[GW] Failed to bind CMD RX on " << bind_ip << ":" << cmd_port << "\n";
    return 1;
  }

  std::cout << "[GW] Serial=" << serial_dev << "@" << serial_baud
            << " | STATE-> " << dst_ip << ":" << dst_state_port
            << " | CMD<- " << bind_ip << ":" << cmd_port
            << " | rate=" << hz << " Hz\n";

  using clock = std::chrono::steady_clock;
  const auto dt = std::chrono::duration<double>(1.0 / hz);
  const auto t0 = clock::now();
  auto next = clock::now();

  gateway::CmdPktV1 last_cmd{};
  bool have_cmd = false;
  auto last_cmd_time = clock::now();

  uint32_t state_seq = 0;

  while (g_run.load()) {
    // ---- receive latest CMD (non-blocking) ----
    for (;;) {
      gateway::CmdPktV1 c{};
      size_t n = 0;
      if (!cmd_rx.try_recv(&c, sizeof(c), n)) break;
      if (n == sizeof(c)) {
        last_cmd = c;
        have_cmd = true;
        last_cmd_time = clock::now();
      }
    }

    // ---- safety timeout ----
    const double cmd_age = std::chrono::duration<double>(clock::now() - last_cmd_time).count();
    const bool cmd_valid = have_cmd && (cmd_age <= cmd_timeout_s);

    // ---- apply command to board ----
    int m1=0,m2=0,m3=0,m4=0;
    uint16_t beep_ms = 0;
    if (cmd_valid) {
      m1 = clamp_i16(last_cmd.m1, -100, 100);
      m2 = clamp_i16(last_cmd.m2, -100, 100);
      m3 = clamp_i16(last_cmd.m3, -100, 100);
      m4 = clamp_i16(last_cmd.m4, -100, 100);
      beep_ms = last_cmd.beep_ms;
    }
    bot.set_motor(m1, m2, m3, m4);
    if (beep_ms > 0) {
      bot.set_beep((int)beep_ms);
      // optional: clear to avoid repeating beep every cycle
      last_cmd.beep_ms = 0;
    }

    // ---- publish state ----
    const rosmaster::State s = bot.get_state();
    gateway::StatePktV1 pkt{};
    pkt.seq = ++state_seq;
    pkt.t_mono_s = std::chrono::duration<double>(clock::now() - t0).count();

    pkt.ax = s.imu.acc.x; pkt.ay = s.imu.acc.y; pkt.az = s.imu.acc.z;
    pkt.gx = s.imu.gyro.x; pkt.gy = s.imu.gyro.y; pkt.gz = s.imu.gyro.z;
    pkt.mx = s.imu.mag.x; pkt.my = s.imu.mag.y; pkt.mz = s.imu.mag.z;

    pkt.roll = s.att.roll; pkt.pitch = s.att.pitch; pkt.yaw = s.att.yaw;

    pkt.e1 = s.enc.m1; pkt.e2 = s.enc.m2; pkt.e3 = s.enc.m3; pkt.e4 = s.enc.m4;

    pkt.battery_v = s.battery_voltage;

    (void)state_tx.send(&pkt, sizeof(pkt));

    // ---- fixed-rate schedule ----
    next += std::chrono::duration_cast<clock::duration>(dt);
    std::this_thread::sleep_until(next);
  }

  // safety stop on exit
  bot.set_motor(0,0,0,0);
  std::cout << "[GW] Exiting.\n";
  return 0;
}
