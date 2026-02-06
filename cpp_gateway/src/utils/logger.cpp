#include "utils/logger.hpp"
#include "core/basic.hpp"

#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace logger {
namespace {

constexpr const char* kColorRed   = "\x1b[91m";
constexpr const char* kColorYellow= "\x1b[93m";
constexpr const char* kColorGreen = "\x1b[92m";
constexpr const char* kColorBlue  = "\x1b[94m";
constexpr const char* kColorReset = "\x1b[0m";

constexpr size_t kLevelCount = 4;

constexpr size_t level_index(Level level) {
  switch (level) {
    case Level::Debug: return 0;
    case Level::Info:  return 1;
    case Level::Warn:  return 2;
    case Level::Error: return 3;
  }
  return 0;
}

const char* level_name(Level level) {
  switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
  }
  return "UNKNOWN";
}

const char* level_color(Level level) {
  switch (level) {
    case Level::Debug: return kColorBlue;
    case Level::Info:  return kColorGreen;
    case Level::Warn:  return kColorYellow;
    case Level::Error: return kColorRed;
  }
  return kColorReset;
}

std::string format_time(const char* fmt) {
  const auto now = std::chrono::system_clock::now();
  const auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, fmt);
  return oss.str();
}

bool is_blank_string(const std::string& value) {
  for (char c : value) {
    if (!std::isspace(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

struct LogItem {
  Level level;
  std::string message;
};

class Logger {
public:
  static Logger& instance() {
    static Logger inst;
    return inst;
  }

  void set_print_level(int level) {
    if (level <= 0) {
      warn("Invalid print level: " + std::to_string(level));
      return;
    }
    print_level_.store(level);
  }

  void set_log_level(int level) {
    if (level <= 0) {
      warn("Invalid log level: " + std::to_string(level));
      return;
    }
    logging_level_.store(level);
  }

  void set_max_file_size(std::uintmax_t size_bytes) {
    if (size_bytes == 0) {
      warn("Invalid max log size: 0");
      return;
    }
    max_log_size_.store(size_bytes);
  }

  void set_logs_dir(const std::filesystem::path& dir_path) {
    const std::string dir = dir_path.string();
    if (dir.empty() || is_blank_string(dir)) {
      warn("Invalid log directory path");
      return;
    }
    {
      std::scoped_lock lk(config_mtx_);
      log_dir_ = dir_path;
      initialized_ = false;
    }
    ensure_initialized();
  }

  void set_file_logging_enabled(bool enabled) {
    enable_file_logging_.store(enabled);
    std::scoped_lock lk(config_mtx_);
    initialized_ = false;
  }

  void trace(Level level, std::string_view message, const std::source_location& loc) {
    const int lvl = static_cast<int>(level);

    std::filesystem::path file_path(loc.file_name());
    const std::string file_name = file_path.filename().string();

    std::ostringstream ctx;
    ctx << "(" << file_name << ":" << loc.line() << ") " << message << " ";
    const std::string context_msg = ctx.str();

    if (lvl >= print_level_.load()) {
      std::scoped_lock lk(print_mtx_);
      std::cout << level_color(level) << "[" << level_name(level) << "] "
                << context_msg << kColorReset << "\n";
    }

    if (enable_file_logging_.load() && lvl >= logging_level_.load()) {
      ensure_initialized();
      {
        std::scoped_lock lk(queue_mtx_);
        queue_.push_back({level, context_msg});
      }
      queue_cv_.notify_one();
    }
  }

  void close() {
    stop_signal_.store(true);
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

private:
  Logger() = default;
  ~Logger() { close(); }

  void ensure_initialized() {
    const bool enabled = enable_file_logging_.load();
    std::scoped_lock lk(config_mtx_);
    if (initialized_) return;

    date_str_ = format_time("%Y-%m-%d_%H-%M");

    if (enabled) {
      std::error_code ec;
      std::filesystem::create_directories(log_dir_, ec);
      start_worker_if_needed();
    }

    log_files_[level_index(Level::Error)] =
      log_dir_ / ("error_" + date_str_ + ".log");
    log_files_[level_index(Level::Warn)] =
      log_dir_ / ("warn_" + date_str_ + ".log");
    log_files_[level_index(Level::Info)] =
      log_dir_ / ("info_" + date_str_ + ".log");
    log_files_[level_index(Level::Debug)] =
      log_dir_ / ("error_" + date_str_ + ".log");

    initialized_ = true;
  }

  void start_worker_if_needed() {
    if (worker_started_) return;
    worker_ = std::thread(&Logger::worker_loop, this);
    worker_started_ = true;
  }

  std::filesystem::path log_file_for(Level level) {
    ensure_initialized();
    std::scoped_lock lk(config_mtx_);
    return log_files_[level_index(level)];
  }

  void rotate_if_needed(const std::filesystem::path& log_file) {
    std::error_code ec;
    if (!std::filesystem::exists(log_file, ec)) return;
    const auto size = std::filesystem::file_size(log_file, ec);
    if (ec || size <= max_log_size_.load()) return;

    const std::filesystem::path dir = log_file.parent_path();
    const std::string stem = log_file.stem().string();
    const std::string ext = log_file.extension().string();

    for (int i = 1; i < 10000; ++i) {
      std::filesystem::path rotated = dir / (stem + "_" + std::to_string(i) + ext);
      if (!std::filesystem::exists(rotated, ec)) {
        std::filesystem::rename(log_file, rotated, ec);
        break;
      }
    }
  }

  void worker_loop() {
    while (true) {
      LogItem item;
      {
        std::unique_lock lk(queue_mtx_);
        queue_cv_.wait(lk, [&] {
          return stop_signal_.load() || !queue_.empty();
        });

        if (queue_.empty() && stop_signal_.load()) break;
        if (queue_.empty()) continue;

        item = std::move(queue_.front());
        queue_.pop_front();
      }

      const std::filesystem::path log_file = log_file_for(item.level);
      rotate_if_needed(log_file);

      const auto counter = ++msg_counter_;
      const std::string timestamp = format_time("%H:%M:%S");

      std::ostringstream line;
      line << std::setw(6) << std::setfill('0') << counter
           << " [" << timestamp << "] [" << level_name(item.level) << "] "
           << item.message << "\n";

      std::ofstream out(log_file, std::ios::app);
      if (out) out << line.str();
    }
  }

private:
  std::atomic<bool> stop_signal_{false};
  std::thread worker_;
  bool worker_started_{false};

  std::mutex queue_mtx_;
  std::condition_variable queue_cv_;
  std::deque<LogItem> queue_;

  std::mutex config_mtx_;
  bool initialized_{false};
  std::filesystem::path log_dir_{"logs"};
  std::string date_str_;
  std::array<std::filesystem::path, kLevelCount> log_files_{};

  std::atomic<int> print_level_{static_cast<int>(Level::Info)};
  std::atomic<int> logging_level_{static_cast<int>(Level::Debug)};
  std::atomic<bool> enable_file_logging_{true};
  std::atomic<std::uintmax_t> max_log_size_{1'000'000};
  std::atomic<std::uint64_t> msg_counter_{0};

  std::mutex print_mtx_;
};

} // namespace

LogStream::LogStream(Level level, const std::source_location& loc)
  : level_(level), loc_(loc) {}

LogStream::LogStream(LogStream&& other) noexcept
  : level_(other.level_),
    loc_(other.loc_),
    stream_(std::move(other.stream_)),
    active_(other.active_) {
  other.active_ = false;
}

LogStream::~LogStream() {
  if (!active_) return;
  try {
    commit();
  } catch (...) {
    // keep destructor non-throwing
  }
}

void LogStream::commit() {
  if (!active_) return;
  active_ = false;
  trace(level_, stream_.str(), loc_);
}

void set_print_level(Level level) {
  Logger::instance().set_print_level(static_cast<int>(level));
}

void set_print_level(int level) {
  Logger::instance().set_print_level(level);
}

void set_log_level(Level level) {
  Logger::instance().set_log_level(static_cast<int>(level));
}

void set_log_level(int level) {
  Logger::instance().set_log_level(level);
}

void set_max_file_size(std::uintmax_t size_bytes) {
  Logger::instance().set_max_file_size(size_bytes);
}

void set_logs_dir(const std::filesystem::path& dir_path) {
  Logger::instance().set_logs_dir(dir_path);
}

void set_file_logging_enabled(bool enabled) {
  Logger::instance().set_file_logging_enabled(enabled);
}

void trace(Level level,
           std::string_view message,
           const std::source_location& loc) {
  Logger::instance().trace(level, message, loc);
}

LogStream trace(Level level, const std::source_location& loc) {
  return LogStream(level, loc);
}

void debug(std::string_view message, const std::source_location& loc) {
  trace(Level::Debug, message, loc);
}

void info(std::string_view message, const std::source_location& loc) {
  trace(Level::Info, message, loc);
}

void warn(std::string_view message, const std::source_location& loc) {
  trace(Level::Warn, message, loc);
}

void error(std::string_view message, const std::source_location& loc) {
  trace(Level::Error, message, loc);
}

LogStream debug(const std::source_location& loc) {
  return LogStream(Level::Debug, loc);
}

LogStream info(const std::source_location& loc) {
  return LogStream(Level::Info, loc);
}

LogStream warn(const std::source_location& loc) {
  return LogStream(Level::Warn, loc);
}

LogStream error(const std::source_location& loc) {
  return LogStream(Level::Error, loc);
}

void close_logger() {
  Logger::instance().close();
}

} // namespace logger
