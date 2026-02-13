#pragma once
#include "gateway/stop_flag.hpp"
#include "shared_state.hpp"

namespace workers {

class TcpWorker {
public:
  TcpWorker(SharedState& sh, gateway::StopFlag& stop);
  void operator()();

private:
  SharedState& sh_;
  gateway::StopFlag& stop_;
};

} // namespace workers
