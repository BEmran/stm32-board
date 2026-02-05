#include "utils/csv_recorder.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace utils {

CSVRecorder::CSVRecorder(
    const std::filesystem::path& recorder_dir,
    std::string_view prefix,
    std::vector<std::string> header,
    std::size_t buffer_size
)
    : csv_path_(build_path(recorder_dir, prefix))
    , header_(std::move(header))
    , buffer_(std::make_unique<char[]>(buffer_size))
    , buffer_size_(buffer_size)
{
    if (header_.empty()) {
        throw std::invalid_argument("Header cannot be empty");
    }
}

CSVRecorder::~CSVRecorder() noexcept {
    try {
        close();
    } catch (...) {
        // Destructors must not throw
        // Error already logged in close()
    }
}

std::filesystem::path CSVRecorder::build_path(
    const std::filesystem::path& recorder_dir,
    std::string_view prefix
) const {
    // Create directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(recorder_dir, ec);
    if (ec) {
        std::cerr << "Warning: Could not create directory " << recorder_dir 
                  << ": " << ec.message() << '\n';
    }

    // Generate timestamp for filename
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm_buf{};
    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
    #else
        localtime_r(&time_t_now, &tm_buf);
    #endif

    std::ostringstream timestamp;
    timestamp << std::put_time(&tm_buf, "%Y-%m-%d_%H-%M-%S");

    // Build filename
    std::string filename;
    if (!prefix.empty()) {
        filename = std::string(prefix) + "_" + timestamp.str() + ".csv";
    } else {
        filename = timestamp.str() + ".csv";
    }

    auto path = recorder_dir / filename;
    std::cout << "Recording to: " << path << '\n';
    
    return path;
}

bool CSVRecorder::open() {
    if (file_.is_open()) {
        return true; // Already open
    }

    // Open file with custom buffer
    file_.open(csv_path_, std::ios::out | std::ios::trunc);
    
    if (!file_) {
        std::cerr << "Error: Failed to open " << csv_path_ << '\n';
        return false;
    }

    // Set custom buffer for better performance on SBC
    file_.rdbuf()->pubsetbuf(buffer_.get(), buffer_size_);

    // Write header
    if (!header_written_) {
        for (std::size_t i = 0; i < header_.size(); ++i) {
            file_ << header_[i];
            if (i < header_.size() - 1) {
                file_ << ',';
            }
        }
        file_ << '\n';
        header_written_ = true;
    }

    return file_.good();
}

bool CSVRecorder::record(const CSVRow& row) {
    if (!file_.is_open()) {
        std::cerr << "Error: CSVRecorder is not open\n";
        return false;
    }

    return write_row(row);
}

bool CSVRecorder::write_row(const CSVRow& row) {
    // Create a map for O(1) lookup
    std::unordered_map<std::string, std::string> row_map;
    row_map.reserve(row.size());
    for (const auto& [key, value] : row) {
        row_map[key] = value;
    }

    // Write values in header order
    for (std::size_t i = 0; i < header_.size(); ++i) {
        auto it = row_map.find(header_[i]);
        if (it != row_map.end()) {
            file_ << it->second;
        }
        // If key not found, write empty field
        
        if (i < header_.size() - 1) {
            file_ << ',';
        }
    }
    file_ << '\n';

    return file_.good();
}

CSVRow CSVRecorder::actions_to_row(
    const core::Timestamps& ts, 
    const core::Actions& actions
) const {
    return {
        {"t_epoch_s", format_value(ts.epoch_s, 6)},
        {"t_mono_s", format_value(ts.mono_s, 6)},
        {"m1", format_value(actions.motors.m1)},
        {"m2", format_value(actions.motors.m2)},
        {"m3", format_value(actions.motors.m3)},
        {"m4", format_value(actions.motors.m4)},
        {"beep_ms", format_value(actions.beep_ms)},
        {"flags", format_value(actions.flags)}
    };
}

CSVRow CSVRecorder::state_to_row(
    const core::Timestamps& ts, 
    const core::State& state
) const {
    return {
        {"t_epoch_s", format_value(ts.epoch_s, 6)},
        {"t_mono_s", format_value(ts.mono_s, 6)},
        {"ax", format_value(state.imu.acc.x, 6)},
        {"ay", format_value(state.imu.acc.y, 6)},
        {"az", format_value(state.imu.acc.z, 6)},
        {"gx", format_value(state.imu.gyro.x, 6)},
        {"gy", format_value(state.imu.gyro.y, 6)},
        {"gz", format_value(state.imu.gyro.z, 6)},
        {"mx", format_value(state.imu.mag.x, 6)},
        {"my", format_value(state.imu.mag.y, 6)},
        {"mz", format_value(state.imu.mag.z, 6)},
        {"roll_deg", format_value(state.ang.roll, 6)},
        {"pitch_deg", format_value(state.ang.pitch, 6)},
        {"yaw_deg", format_value(state.ang.yaw, 6)},
        {"enc1", format_value(state.enc.e1)},
        {"enc2", format_value(state.enc.e2)},
        {"enc3", format_value(state.enc.e3)},
        {"enc4", format_value(state.enc.e4)}
    };
}

bool CSVRecorder::record_actions(const core::Timestamps& ts, const core::Actions& actions) {
    if (!file_.is_open()) {
        std::cerr << "Error: CSVRecorder is not open\n";
        return false;
    }

    auto row = actions_to_row(ts, actions);
    return write_row(row);
}

bool CSVRecorder::record_state(const core::Timestamps& ts, const core::State& state) {
    if (!file_.is_open()) {
        std::cerr << "Error: CSVRecorder is not open\n";
        return false;
    }

    auto row = state_to_row(ts, state);
    return write_row(row);
}

void CSVRecorder::flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

void CSVRecorder::close() noexcept {
    if (file_.is_open()) {
        try {
            file_.flush();
            file_.close();
        } catch (const std::exception& e) {
            std::cerr << "Error closing CSV file: " << e.what() << '\n';
        }
    }
}

// Static timestamp utilities

core::Timestamps CSVRecorder::now() noexcept {
    return {
        .epoch_s = epoch_now(),
        .mono_s = monotonic_now()
    };
}

double CSVRecorder::epoch_now() noexcept {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto duration = now.time_since_epoch();
    return duration_cast<std::chrono::duration<double>>(duration).count();
           
}

double CSVRecorder::monotonic_now() noexcept {
    using namespace std::chrono;
    auto now = steady_clock::now();
    auto duration = now.time_since_epoch();
    return duration_cast<std::chrono::duration<double>>(duration).count();
}

} // namespace utils
