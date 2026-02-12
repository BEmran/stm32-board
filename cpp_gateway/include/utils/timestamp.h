#pragma once
#include "core/basic.hpp"
#include <string>
#include <string_view>

namespace utils {
/**
 * @brief Get current timestamps
 * @return Timestamps with epoch and monotonic time
 */
 core::Timestamps now() noexcept;

/**
 * @brief Get epoch timestamp in seconds
 */
 double epoch_now() noexcept;

/**
 * @brief Get monotonic timestamp in seconds
 */
 double monotonic_now() noexcept;

 std::string timestamp_string(std::string_view fmt = "%Y-%m-%d_%H-%M-%S");
} // namespace utils
