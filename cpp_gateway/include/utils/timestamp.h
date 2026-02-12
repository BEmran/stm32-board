#pragma once
#include "core/basic.hpp"
#include <string>
#include <string_view>

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

[[nodiscard]] std::string timestamp_string(std::string_view fmt = "%Y-%m-%d_%H-%M-%S");
} // namespace utils
