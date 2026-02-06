#include "core/basic.hpp"
#include "rosmaster/rosmaster.hpp"
#include "utils/csv_recorder.hpp"
#include "utils/logger.hpp"
#include "utils/timestamp.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;
constexpr double PRINT_DURATION = 1.0;

bool should_print(double duration) {
  static double last = utils::monotonic_now();
  double now = utils::monotonic_now();
  if (now - last > duration) {
    last = now;
    return true;
  }
  return false;
}

int main() {
  rosmaster::Rosmaster bot;
  rosmaster::Config cfg;
  cfg.device = "/dev/ttyUSB0";
  cfg.debug = true;

  if (!bot.connect(cfg)) {
    logger::error() << "Failed to connect\n";
    return 1;
  }

  bot.start();
  bot.set_auto_report_state(true, false);

  logger::debug() << "Version: " << bot.get_version() << "\n";

  // Create recorder
  utils::CSVRecorder actions_recorder("./recorder/cmd", "actions", headers::ACTIONS);
  utils::CSVRecorder state_recorder("./recorder/state", "state", headers::STATE);
  
  // Open the recorder
  if (!actions_recorder.open() or !state_recorder.open()) {
      logger::error() << "Failed to open recorder\n";
      return EXIT_FAILURE;
  }

  // Simulate recording actions
  while (true) {
      core::State state = bot.get_state();
      core::Actions actions;
      auto ts = utils::now();
      actions_recorder.record_actions(ts, actions);
      state_recorder.record_state(ts, state);
      if (should_print(PRINT_DURATION)) {
        logger::info() << "states and actions are logged, up time = " << utils::monotonic_now();
      }
      std::this_thread::sleep_for(100ms);
  }

  actions_recorder.close();
  state_recorder.close();
  logger::debug() << "Actions saved to: " << actions_recorder.path() << " and " << state_recorder.path() << "\n";
}

