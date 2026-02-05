#include "rosmaster/basic.hpp"
#include "rosmaster/rosmaster.hpp"
#include <iostream>
#include <thread>

int main() {
  rosmaster::Rosmaster bot;
  rosmaster::Config cfg;
  cfg.device = "/dev/ttyUSB0";
  cfg.debug = true;

  if (!bot.connect(cfg)) {
    std::cerr << "Failed to connect\n";
    return 1;
  }

  bot.start();
  bot.set_auto_report_state(true, false);

  std::cout << "Version: " << bot.get_version() << "\n";

  while (true) {
    rosmaster::State state = bot.get_state();

    std::cout << "ax="<<state.imu.acc.x<<" ay="<<state.imu.acc.y<<" az="<<state.imu.acc.z
              << " roll="<<state.imu.gyro.x<<" pitch="<<state.imu.gyro.y<<" yaw="<<state.imu.gyro.z
              << " e1="<<state.enc.m1<<" e2="<<state.enc.m2 << "\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
