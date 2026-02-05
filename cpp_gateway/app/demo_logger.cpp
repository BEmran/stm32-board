#include "rosmaster/basic.hpp"
#include "rosmaster/rosmaster.hpp"
#include "utils/csv_recorder.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

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

  // Create recorder
  utils::CSVRecorder actions_recorder("/recorder/cmd", "actions", utils::headers::ACTIONS);
  utils::CSVRecorder state_recorder("/recorder/state", "state", utils::headers::STATE);
  
  // Open the recorder
  if (!actions_recorder.open() or !state_recorder.open()) {
      std::cerr << "Failed to open recorder\n";
      return EXIT_FAILURE;
  }

  // Simulate recording actions
  while (true) {
      core::State state = bot.get_state();
      core::Actions actions;
      auto ts = utils::CSVRecorder::now();
      actions_recorder.record_actions(ts, actions);
      state_recorder.record_state(ts, state);
      std::this_thread::sleep_for(100ms);
  }

  actions_recorder.close();
  state_recorder.close();
  std::cout << "Actions saved to: " << actions_recorder.path() << " and " << state_recorder.path() << "\n";
}

