#pragma once
#include "utils/logger.hpp"

#include <cstdint>

#ifdef __linux__
  #include <pthread.h>
  #include <sched.h>
#endif

namespace utils {

/**
 * @brief Best-effort set real-time FIFO priority for the current thread.
 *
 * Portable behavior:
 * - On Linux: attempts pthread_setschedparam(SCHED_FIFO, prio).
 * - Elsewhere: no-op.
 *
 * @param prio 1..99 for FIFO, 0 to disable (no-op).
 * @return true if applied, false otherwise.
 */
inline bool try_set_fifo_priority(int prio) noexcept {
  if (prio <= 0) return false;
#ifdef __linux__
  sched_param sp{};
  sp.sched_priority = prio;
  const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
  if (rc != 0) {
    logger::warn() << "[SCHED] Failed to set SCHED_FIFO priority " << prio
                   << " (need CAP_SYS_NICE or root). rc=" << rc << "\n";
    return false;
  }
  logger::info() << "[SCHED] SCHED_FIFO priority set to " << prio << "\n";
  return true;
#else
  (void)prio;
  return false;
#endif
}

} // namespace utils
