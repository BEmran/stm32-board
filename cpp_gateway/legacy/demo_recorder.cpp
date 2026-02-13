#include "core/basic.hpp"
#include "rosmaster/rosmaster.hpp"
#include "utils/csv_recorder.hpp"
#include "utils/logger.hpp"
#include "utils/timestamp.h"
#include "helpper.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <string_view>

constexpr double PRINT_DURATION = 1.0;
using namespace std::chrono_literals;
constexpr std::string_view RECORDER_PATH{"./recorder"};

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
  utils::CSVActionsRecorder actions_recorder(RECORDER_PATH);
  utils::CSVStatesRecorder state_recorder(RECORDER_PATH);
  
  // Open the recorder
  if (!actions_recorder.open() or !state_recorder.open()) {
      logger::error() << "Failed to open recorder\n";
      return EXIT_FAILURE;
  }
  helpper::Print print_info(PRINT_DURATION);
  
  // Simulate recording actions
  while (true) {
      const auto state = bot.get_state();
      core::Actions actions{};
      const auto ts = utils::now();
      actions_recorder.record_actions(ts, actions);
      state_recorder.record_state(ts, state);
      if (print_info.check()) {
        logger::info() << "states and actions are logged, up time = " << utils::monotonic_now();
      }
      std::this_thread::sleep_for(100ms);
  }

  actions_recorder.close();
  state_recorder.close();
  logger::debug() << "Actions saved to: " << actions_recorder.path() << " and " << state_recorder.path() << "\n";
}

