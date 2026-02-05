#pragma once

#include "core/basic.hpp"
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <source_location>
#include <sstream>
#include <string_view>
#include <utility>
namespace logger {

enum class Level : int {
  Debug = 10,
  Info  = 20,
  Warn  = 30,
  Error = 40
};

// Configuration
void set_print_level(Level level);
void set_print_level(int level);

void set_log_level(Level level);
void set_log_level(int level);

void set_max_file_size(std::uintmax_t size_bytes);
void set_logs_dir(const std::filesystem::path& dir_path);
void set_file_logging_enabled(bool enabled);

// Logging API
void trace(Level level,
           std::string_view message,
           const std::source_location& loc = std::source_location::current());

class LogStream {
public:
  LogStream(Level level, const std::source_location& loc);
  LogStream(LogStream&& other) noexcept;
  LogStream(const LogStream&) = delete;
  LogStream& operator=(const LogStream&) = delete;
  LogStream& operator=(LogStream&&) = delete;
  ~LogStream();

  template <typename T>
  LogStream& operator<<(T&& value) {
    stream_ << std::forward<T>(value);
    return *this;
  }

  using Manip = std::ostream& (*)(std::ostream&);
  LogStream& operator<<(Manip manip) {
    manip(stream_);
    return *this;
  }

  void commit();

private:
  Level level_;
  std::source_location loc_;
  std::ostringstream stream_;
  bool active_{true};
};

LogStream trace(Level level,
                const std::source_location& loc = std::source_location::current());

void debug(std::string_view message,
           const std::source_location& loc = std::source_location::current());
void info(std::string_view message,
          const std::source_location& loc = std::source_location::current());
void warn(std::string_view message,
          const std::source_location& loc = std::source_location::current());
void error(std::string_view message,
           const std::source_location& loc = std::source_location::current());

LogStream debug(const std::source_location& loc = std::source_location::current());
LogStream info(const std::source_location& loc = std::source_location::current());
LogStream warn(const std::source_location& loc = std::source_location::current());
LogStream error(const std::source_location& loc = std::source_location::current());

// Flush and stop the logger thread (optional at shutdown)
void close_logger();

} // namespace logger
