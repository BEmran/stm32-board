#include "controller.hpp"

#include <cmath>
#include <chrono>
#include <iostream>
#include <thread>

namespace app
{

  Controller::Controller(const ControllerConfig &cfg) : cfg_(cfg) {}

  bool Controller::init()
  {
    if (!rx_.bind_rx(cfg_.state_port, /*nonblocking=*/true))
    {
      std::cerr << "[CTRL] Failed to bind RX port " << cfg_.state_port << "\n";
      return false;
    }
    if (!tx_.set_tx_destination(cfg_.ip, cfg_.cmd_port))
    {
      std::cerr << "[CTRL] Failed to set TX destination " << cfg_.ip << ":" << cfg_.cmd_port << "\n";
      return false;
    }
    std::cout << "[CTRL] RX state: 0.0.0.0:" << cfg_.state_port
              << "  TX cmd: " << cfg_.ip << ":" << cfg_.cmd_port
              << "  rate: " << cfg_.hz << " Hz\n";
    return true;
  }

  int Controller::run()
  {
    using clock = std::chrono::steady_clock;
    const auto dt = std::chrono::duration<double>(1.0 / cfg_.hz);

    auto next = clock::now();
    auto last_print = clock::now();
    int rx_count = 0;

    while (true)
    {
      // Drain incoming packets (keep latest)
      for (;;)
      {
        StatePkt s{};
        size_t n = 0;
        if (!rx_.try_recv(&s, sizeof(s), n))
          break;
        if (n == sizeof(s))
        {
          last_state_ = s;
          have_state_ = true;
          rx_count++;
        }
      }

      // Build command (safe default: zeros)
      CmdPkt cmd{};
      cmd.seq = ++cmd_seq_;
      cmd.m1 = 0;
      cmd.m2 = 0;
      cmd.m3 = 0;
      cmd.m4 = 0;
      cmd.beep_ms = 10;
      cmd.flags = 0;

      // Optional: small test command (disabled by default)
      // if (have_state_) {
      //   double u = -50.0 * (double)last_state_.roll; // toy P
      //   int ui = (int)std::llround(u);
      //   if (ui < 0) ui = 0;      // protocol uses uint16
      //   if (ui > 200) ui = 200;  // small safe range
      //   cmd.m1 = (uint16_t)ui;
      // }

      (void)tx_.send(&cmd, sizeof(cmd));

      // periodic print
      auto now = clock::now();
      if (std::chrono::duration<double>(now - last_print).count() >= cfg_.print_period_s)
      {
        last_print = now;
        if (have_state_)
        {
          print_state(last_state_);
        }
        else
        {
          std::cout << "[CTRL] rx/s=" << rx_count << " (no state yet)\n";
        }
        rx_count = 0;
      }

      // fixed-rate scheduling
      next += std::chrono::duration_cast<clock::duration>(dt);
      std::this_thread::sleep_until(next);
    }
    return 0;
  }

} // namespace app
