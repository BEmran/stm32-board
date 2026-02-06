#include "core/basic.hpp"
#include "rosmaster/rosmaster.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <thread>

int main()
{
  rosmaster::Rosmaster bot;
  rosmaster::Config cfg;
  cfg.device = "/dev/ttyUSB0";
  cfg.debug = true;

  if (!bot.connect(cfg))
  {
    logger::error() << "Failed to connect\n";
    return 1;
  }

  bot.start();
  bot.set_auto_report_state(true, false);

  logger::info() << "Version: " << bot.get_version() << "\n";

  while (true)
  {
    core::State state = bot.get_state();

    logger::info() << "ax=" << state.imu.acc.x << " ay=" << state.imu.acc.y << " az=" << state.imu.acc.z
                   << " roll=" << state.imu.gyro.x << " pitch=" << state.imu.gyro.y << " yaw=" << state.imu.gyro.z
                   << " e1=" << state.enc.e1 << " e2=" << state.enc.e2;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
