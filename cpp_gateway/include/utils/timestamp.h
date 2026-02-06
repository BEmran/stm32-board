#pragma once
#include "core/basic.hpp"
#include <sstream>

namespace utils {
/**
 * @brief Get current timestamps
 * @return Timestamps with epoch and monotonic time
 */
[[nodiscard]] core::Timestamps now() noexcept;

/**
 * @brief Get epoch timestamp in seconds
 */
[[nodiscard]] double epoch_now() noexcept;

/**
 * @brief Get monotonic timestamp in seconds
 */
[[nodiscard]] double monotonic_now() noexcept;

std::ostringstream timestamp_string (const char* fmt = "%Y-%m-%d_%H-%M-%S");
}