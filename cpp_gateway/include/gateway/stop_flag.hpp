#pragma once
#include <atomic>

namespace gateway {

class StopFlag {
public:
  void request_stop() noexcept { stop_.store(true, std::memory_order_release); }
  bool stop_requested() const noexcept { return stop_.load(std::memory_order_acquire); }

private:
  std::atomic<bool> stop_{false};
};

} // namespace gateway
