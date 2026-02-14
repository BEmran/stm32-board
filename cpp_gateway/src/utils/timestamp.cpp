#include "utils/timestamp.h"
#include <chrono>
#include <ctime>
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
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

double monotonic_now() noexcept {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string timestamp_string(std::string_view fmt) {
    // Generate timestamp for filename
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm_buf{};
    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
    #else
        localtime_r(&time_t_now, &tm_buf);
    #endif

    std::ostringstream oss;
    const std::string fmt_str(fmt);
    oss << std::put_time(&tm_buf, fmt_str.c_str());
    return oss.str();
}
}
