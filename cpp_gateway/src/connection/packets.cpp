
#include "connection/packets.hpp"

#include <bit>
#include <cstring>

namespace connection
{
  StatesPkt state_to_state_pkt(uint32_t seq, float t_mono_s, core::States state)
  {
    return StatesPkt{
        .seq = seq,
        .t_mono_s = t_mono_s,
        .state = state
      };
  }
} // namespace connection
