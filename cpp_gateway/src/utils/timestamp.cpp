#include "utils/timestamp.h"
#include <chrono>
#include <ctime>
#include <string>
#include <iostream>
#include <iomanip>

namespace utils {
core::Timestamps now() noexcept {
    return {
        .epoch_s = epoch_now(),
        .mono_s = monotonic_now()
    };
}

double epoch_now() noexcept {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto duration = now.time_since_epoch();
    return duration_cast<std::chrono::duration<double>>(duration).count();
           
}

double monotonic_now() noexcept {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return duration_cast<std::chrono::duration<double>>(
        steady_clock::now() - t0
    ).count();
}

std::ostringstream timestamp_string (const char* fmt) {
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
    return timestamp;
}
}