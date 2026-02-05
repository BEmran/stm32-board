#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/basic.hpp"
namespace utils {

// Concepts for type safety
template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

template <typename T>
concept Streamable = requires(std::ostream& os, const T& t) {
    { os << t } -> std::same_as<std::ostream&>;
};

// CSV row as key-value pairs
using CSVRow = std::vector<std::pair<std::string, std::string>>;

/**
 * @brief High-performance CSV recorder for robotics data logging
 * 
 * Features:
 * - RAII-compliant resource management
 * - Move-only semantics (non-copyable)
 * - Buffered I/O with configurable flush policy
 * - Type-safe serialization
 * - Exception-safe operations
 * - Timestamp generation utilities
 */
class CSVRecorder {
public:
    /**
     * @brief Construct a CSV recorder
     * @param recorder_dir Directory to store CSV files
     * @param prefix Optional filename prefix
     * @param header Column headers
     * @param buffer_size Buffer size in bytes (default: 64KB for SBC optimization)
     */
    explicit CSVRecorder(
        const std::filesystem::path& recorder_dir,
        std::string_view prefix,
        std::vector<std::string> header,
        std::size_t buffer_size = 65536
    );

    // Non-copyable, move-only
    CSVRecorder(const CSVRecorder&) = delete;
    CSVRecorder& operator=(const CSVRecorder&) = delete;
    CSVRecorder(CSVRecorder&&) noexcept = default;
    CSVRecorder& operator=(CSVRecorder&&) noexcept = default;

    ~CSVRecorder() noexcept;

    /**
     * @brief Open the CSV file and write header
     * @return true if successful, false otherwise
     */
    [[nodiscard]] bool open();

    /**
     * @brief Check if recorder is open and ready
     */
    [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }

    /**
     * @brief Record a row of data
     * @param row Key-value pairs matching header columns
     * @return true if successful, false otherwise
     */
    bool record(const CSVRow& row);

    /**
     * @brief Record actions data with timestamps
     * @param ts Timestamps
     * @param actions Action commands
     * @return true if successful, false otherwise
     */
    bool record_actions(const core::Timestamps& ts, const core::Actions& actions);

    /**
     * @brief Record state data with timestamps
     * @param ts Timestamps
     * @param state Robot state
     * @return true if successful, false otherwise
     */
    bool record_state(const core::Timestamps& ts, const core::State& state);

    /**
     * @brief Flush data to disk
     */
    void flush();

    /**
     * @brief Close the file
     */
    void close() noexcept;

    /**
     * @brief Get the path to the CSV file
     */
    [[nodiscard]] const std::filesystem::path& path() const noexcept { 
        return csv_path_; 
    }

    // Utility functions for timestamp generation
    
    /**
     * @brief Get current timestamps
     * @return Timestamps with epoch and monotonic time
     */
    [[nodiscard]] static core::Timestamps now() noexcept;

    /**
     * @brief Get epoch timestamp in seconds
     */
    [[nodiscard]] static double epoch_now() noexcept;

    /**
     * @brief Get monotonic timestamp in seconds
     */
    [[nodiscard]] static double monotonic_now() noexcept;

private:
    /**
     * @brief Build file path with timestamp
     */
    [[nodiscard]] std::filesystem::path build_path(
        const std::filesystem::path& recorder_dir,
        std::string_view prefix
    ) const;

    /**
     * @brief Convert Actions to CSV row
     */
    [[nodiscard]] CSVRow actions_to_row(
        const core::Timestamps& ts, 
        const core::Actions& actions
    ) const;

    /**
     * @brief Convert State to CSV row
     */
    [[nodiscard]] CSVRow state_to_row(
        const core::Timestamps& ts, 
        const core::State& state
    ) const;

    /**
     * @brief Format a numeric value with precision
     */
    template <Numeric T>
    [[nodiscard]] static std::string format_value(T value, int precision = 6);

    /**
     * @brief Write a row to file
     */
    bool write_row(const CSVRow& row);

    std::filesystem::path csv_path_;
    std::vector<std::string> header_;
    std::ofstream file_;
    std::unique_ptr<char[]> buffer_;
    std::size_t buffer_size_;
    bool header_written_{false};
};

// Template implementation

template <Numeric T>
std::string CSVRecorder::format_value(T value, int precision) {
    std::ostringstream oss;
    if constexpr (std::is_floating_point_v<T>) {
        oss.precision(precision);
        oss << std::fixed << value;
    } else {
        oss << value;
    }
    return oss.str();
}

// Header constants for convenience
namespace headers {
    inline const std::vector<std::string> ACTIONS = {
        "t_epoch_s", "t_mono_s", "m1", "m2", "m3", "m4", "beep_ms", "flags"
    };

    inline const std::vector<std::string> STATE = {
        "t_epoch_s", "t_mono_s",
        "ax", "ay", "az",
        "gx", "gy", "gz",
        "mx", "my", "mz",
        "roll_deg", "pitch_deg", "yaw_deg",
        "enc1", "enc2", "enc3", "enc4"
    };
} // namespace headers

} // namespace utils
