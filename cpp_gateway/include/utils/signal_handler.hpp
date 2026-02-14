#pragma once
/**
 * @file signal_handler.hpp
 * @brief RAII POSIX signal handler for clean shutdown.
 *
 * The gateway uses a shared StopFlag. This helper installs SIGINT/SIGTERM handlers
 * that flip the stop flag and then restores the previous handlers on destruction.
 */
#include "gateway/stop_flag.hpp"

#include <csignal>

namespace utils {

class SignalHandler {
public:
  explicit SignalHandler(gateway::StopFlag& stop) : stop_(stop) {
    instance_ = this;
    old_int_  = std::signal(SIGINT,  &SignalHandler::on_signal);
    old_term_ = std::signal(SIGTERM, &SignalHandler::on_signal);
  }

  ~SignalHandler() noexcept {
    std::signal(SIGINT,  old_int_);
    std::signal(SIGTERM, old_term_);
    instance_ = nullptr;
  }

  SignalHandler(const SignalHandler&) = delete;
  SignalHandler& operator=(const SignalHandler&) = delete;

private:
  static void on_signal(int) {
    if (instance_) instance_->stop_.request_stop();
  }

  gateway::StopFlag& stop_;
  using Handler = void(*)(int);
  Handler old_int_{SIG_DFL};
  Handler old_term_{SIG_DFL};
  static inline SignalHandler* instance_{nullptr};
};

} // namespace utils
