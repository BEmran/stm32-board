#pragma once
/**
 * @file rate_limiter.hpp
 * @brief Small helper for fixed-rate loops (soft real-time).
 *
 * Portable: uses std::chrono + sleep_until. No platform-specific timers.
 *
 * Design note:
 *  - sleep() uses a monotonic "next tick" schedule (steady_clock)
 *  - if the loop runs late, we *skip ahead* rather than "burst catching up"
 *    (prevents sudden bursts that can overload I/O pipelines).
 */
#include <chrono>
#include <cstdint>
#include <thread>

namespace utils {

/**
 * @brief Rate limiter for periodic loops with simple lateness statistics.
 *
 * Usage:
 *   utils::RateLimiter rl(200.0);
 *   rl.reset();
 *   while (...) {
 *     ... do work ...
 *     rl.sleep();
 *   }
 */
class RateLimiter {
public:
  using clock = std::chrono::steady_clock;

  RateLimiter() = default;
  explicit RateLimiter(double hz) { set_hz(hz); reset(); }

  void set_hz(double hz) noexcept {
    hz_ = (hz > 0.0) ? hz : 1.0;
  }

  double hz() const noexcept { return hz_; }

  /// Reset schedule. Call once at loop start.
  void reset() noexcept {
    next_ = clock::now();
    initialized_ = true;
    late_ticks_ = 0;
    skipped_ticks_ = 0;
    last_late_s_ = 0.0;
    max_late_s_ = 0.0;
  }

  /// Number of ticks where loop was late enough to skip.
  std::uint64_t late_ticks() const noexcept { return late_ticks_; }

  /// Total skipped ticks due to overruns.
  std::uint64_t skipped_ticks() const noexcept { return skipped_ticks_; }

  /// Lateness (seconds) on the most recent skip.
  double last_late_s() const noexcept { return last_late_s_; }

  /// Max lateness observed (seconds).
  double max_late_s() const noexcept { return max_late_s_; }

  /**
   * @brief Sleep until the next tick, with overrun handling.
   *
   * If the loop is late by >= one full period, we skip missed periods and
   * schedule from "now" to avoid burst catching up.
   */
  void sleep() {
    if (!initialized_) reset();

    const auto period = std::chrono::duration<double>(1.0 / hz_);
    const auto period_d = std::chrono::duration_cast<clock::duration>(period);

    // schedule next tick
    next_ += period_d;

    const auto now = clock::now();
    if (now > next_) {
      // overrun: skip ahead to avoid burst execution
      const auto late = now - next_;
      const double late_s = std::chrono::duration<double>(late).count();
      last_late_s_ = late_s;
      if (late_s > max_late_s_) max_late_s_ = late_s;
      ++late_ticks_;

      // Estimate how many periods were missed; conservative (+1).
      const auto missed = static_cast<std::uint64_t>(
          late_s / std::chrono::duration<double>(period_d).count()) + 1ULL;
      skipped_ticks_ += missed;

      // restart schedule from now
      next_ = now + period_d;
    }

    std::this_thread::sleep_until(next_);
  }

private:
  double hz_{1.0};
  clock::time_point next_{};
  bool initialized_{false};

  std::uint64_t late_ticks_{0};
  std::uint64_t skipped_ticks_{0};
  double last_late_s_{0.0};
  double max_late_s_{0.0};
};

} // namespace utils
