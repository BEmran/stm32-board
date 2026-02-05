#include "rosmaster/rosmaster.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  rosmaster::Rosmaster bot;

  if (!bot.connect("/dev/ttyUSB0", 115200, true)) {
    std::cerr << "Failed to open serial\n";
    return 1;
  }

  bot.startReceiveThread();

  // enable auto-report (like Python says: sends packets periodically)
  bot.setAutoReport(true, false);

  bot.setBeep(50);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  bot.requestVersion();

  for (;;) {
    auto s = bot.getState();
    std::cout
      << "ax=" << s.imu.ax << " ay=" << s.imu.ay << " az=" << s.imu.az
      << " gx=" << s.imu.gx << " gy=" << s.imu.gy << " gz=" << s.imu.gz
      << " roll=" << s.ang.roll << " pitch=" << s.ang.pitch << " yaw=" << s.ang.yaw
      << " e1=" << s.enc.e1 << " e2=" << s.enc.e2 << " e3=" << s.enc.e3 << " e4=" << s.enc.e4
      << " Vbat=" << s.spd.battery_voltage
      << " ver=" << s.version
      << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
