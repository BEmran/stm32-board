#include "gateway/stop_flag.hpp"
#include "gateway/runtime_config.hpp"
#include "gateway/enums.hpp"

#include "workers/shared_state.hpp"
#include "workers/usb_worker.hpp"
#include "workers/tcp_worker.hpp"
#include "workers/controller_worker.hpp"
#include "workers/log_worker.hpp"

#include "utils/logger.hpp"
#include "utils/signal_handler.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>


static bool parse_hex_u8(std::string_view s, uint8_t& out) {
  unsigned int v = 0;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    if (std::sscanf(std::string(s).c_str(), "%x", &v) == 1 && v <= 0xFF) {
      out = static_cast<uint8_t>(v);
      return true;
    }
  }
  if (std::sscanf(std::string(s).c_str(), "%u", &v) == 1 && v <= 0xFF) {
    out = static_cast<uint8_t>(v);
    return true;
  }
  return false;
}

static gateway::ControlMode parse_control_mode(std::string_view s) {
  if (s == "pass") return gateway::ControlMode::PASS_THROUGH_CMD;
  if (s == "auto") return gateway::ControlMode::AUTONOMOUS;
  if (s == "setpoint") return gateway::ControlMode::AUTONOMOUS_WITH_REMOTE_SETPOINT;
  return gateway::ControlMode::PASS_THROUGH_CMD;
}

static gateway::UsbTimeoutMode parse_usb_timeout_mode(std::string_view s) {
  if (s == "disable") return gateway::UsbTimeoutMode::DISABLE;
  return gateway::UsbTimeoutMode::ENFORCE;
}

static void print_help(const char* argv0) {
  std::printf(
    "Usage: %s [options]\n"
    "  --serial /dev/ttyUSB0\n"
    "  --baud 115200\n"
    "  --bind_ip 0.0.0.0\n"
    "  --state_port 30001\n"
    "  --cmd_port 30002\n"
    "  --usb_hz 200\n"
    "  --tcp_hz 200\n"
    "  --ctrl_hz 200\n"
    "  --hz 200                 (back-compat: sets all three)\n"
    "  --cmd_timeout 0.2\n"
    "  --usb_timeout_mode enforce|disable\n"
    "  --control_mode pass|auto|setpoint\n"
    "  --binary_log 1|0\n"
    "  --log_path ./logs/gateway.bin\n"
    "  --flag_event_mask 0x07\n"
    "  --flag_start_bit N\n"
    "  --flag_stop_bit N\n"
    "  --flag_reset_bit N\n",
    argv0
  );
}

int main(int argc, char** argv) {
  auto cfg = std::make_shared<gateway::RuntimeConfig>();

  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    auto need = [&](std::string_view name) -> std::string_view {
      if (i + 1 >= argc) {
        logger::error() << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };

    if (a == "--serial") cfg->serial_dev = std::string(need(a));
    else if (a == "--baud") cfg->serial_baud = std::stoi(std::string(need(a)));
    else if (a == "--bind_ip") cfg->bind_ip = std::string(need(a));
    else if (a == "--state_port") cfg->state_port = static_cast<uint16_t>(std::stoi(std::string(need(a))));
    else if (a == "--cmd_port") cfg->cmd_port = static_cast<uint16_t>(std::stoi(std::string(need(a))));

    else if (a == "--usb_hz") cfg->usb_hz = std::stod(std::string(need(a)));
    else if (a == "--tcp_hz") cfg->tcp_hz = std::stod(std::string(need(a)));
    else if (a == "--ctrl_hz") cfg->ctrl_hz = std::stod(std::string(need(a)));
    else if (a == "--hz") {
      const double hz = std::stod(std::string(need(a)));
      cfg->usb_hz = hz; cfg->tcp_hz = hz; cfg->ctrl_hz = hz;
    }

    else if (a == "--cmd_timeout") cfg->cmd_timeout_s = std::stod(std::string(need(a)));
    else if (a == "--usb_timeout_mode") cfg->usb_timeout_mode = parse_usb_timeout_mode(need(a));
    else if (a == "--control_mode") cfg->control_mode = parse_control_mode(need(a));
    else if (a == "--binary_log") cfg->binary_log = (std::stoi(std::string(need(a))) != 0);
    else if (a == "--log_path") cfg->log_path = std::string(need(a));

    else if (a == "--flag_event_mask") {
      uint8_t v = 0;
      if (!parse_hex_u8(need(a), v)) {
        logger::error() << "Invalid --flag_event_mask\n";
        return 2;
      }
      cfg->flag_event_mask = v;
    }
    else if (a == "--flag_start_bit") cfg->flag_start_bit = std::stoi(std::string(need(a)));
    else if (a == "--flag_stop_bit")  cfg->flag_stop_bit  = std::stoi(std::string(need(a)));
    else if (a == "--flag_reset_bit") cfg->flag_reset_bit = std::stoi(std::string(need(a)));

    else if (a == "--help") { print_help(argv[0]); return 0; }
    else {
      logger::error() << "Unknown arg: " << a << "\n";
      print_help(argv[0]);
      return 2;
    }
  }

  workers::SharedState sh;
  sh.cfg.store(cfg, std::memory_order_release);

  workers::SystemState sys{};
  sys.running = true; // default armed; you can set false if you want "start" event to arm
  sys.control_mode = cfg->control_mode;
  sh.system_state.store(sys);

  gateway::StopFlag stop;
  utils::SignalHandler sig(stop);
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN); // avoid termination on broken TCP pipe
#endif

  logger::info() << "[MAIN] Starting threaded gateway.\n";

  std::thread t_usb(workers::UsbWorker(sh, stop));
  std::thread t_tcp(workers::TcpWorker(sh, stop));
  std::thread t_ctrl(workers::ControllerWorker(sh, stop));
  std::thread t_log(workers::LogWorker(sh, stop));

  // Main thread waits for a stop request (SIGINT/SIGTERM or a worker requesting stop).
  // This avoids deadlocks if any worker blocks in a join before the stop flag is set.
  while (!stop.stop_requested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Ensure the stop flag is set for all workers.
  stop.request_stop();

  // Join workers (order does not matter now that all are stop-aware).
  if (t_tcp.joinable()) t_tcp.join();
  if (t_ctrl.joinable()) t_ctrl.join();
  if (t_usb.joinable()) t_usb.join();
  if (t_log.joinable()) t_log.join();

  logger::info() << "[MAIN] Shutdown complete.\n";
  return 0;
}
