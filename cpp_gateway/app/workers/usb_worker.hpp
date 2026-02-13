#pragma once
#include "gateway/stop_flag.hpp"
#include "shared_state.hpp"

namespace workers {

struct UsbWorkerParams {
  size_t max_hw_events_per_cycle{8};
};

class UsbWorker {
public:
  UsbWorker(SharedState& sh, gateway::StopFlag& stop, UsbWorkerParams p = {});
  void operator()();

private:
  SharedState& sh_;
  gateway::StopFlag& stop_;
  UsbWorkerParams p_;
};

} // namespace workers
