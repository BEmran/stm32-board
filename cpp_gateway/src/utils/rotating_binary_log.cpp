#include "utils/rotating_binary_log.hpp"
#include "utils/logger.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

namespace utils {

static std::string now_tag() {
  using clock = std::chrono::system_clock;
  const auto t = clock::to_time_t(clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
  return oss.str();
}

bool RotatingBinaryLog::open(const std::string& base_path, uint64_t max_bytes, uint32_t keep_files) {
  close();

  base_path_ = std::filesystem::path(base_path);
  dir_ = base_path_.parent_path();
  if (dir_.empty()) dir_ = ".";
  stem_ = base_path_.stem().string();
  ext_ = base_path_.extension().string();
  if (ext_.empty()) ext_ = ".bin";

  max_bytes_  = max_bytes;
  keep_files_ = keep_files;
  index_ = 0;
  bytes_written_ = 0;
  session_tag_ = now_tag();

  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);

  return open_new_file();
}

void RotatingBinaryLog::close() {
  writer_.close();
}

bool RotatingBinaryLog::rotate_if_needed(uint64_t bytes_to_add) {
  if (!writer_.is_open()) return false;
  if (max_bytes_ == 0) return true;
  if (bytes_written_ + bytes_to_add <= max_bytes_) return true;

  writer_.close();
  return open_new_file();
}

bool RotatingBinaryLog::open_new_file() {
  const std::string name = stem_ + "_" + session_tag_ + "_" + std::to_string(index_++) + ext_;
  const auto path = dir_ / name;

  if (!writer_.open(path.string())) {
    logger::warn() << "[LOG] Failed to open binary log: " << path.string() << "\n";
    return false;
  }

  bytes_written_ = sizeof(FileHeader);

  // cleanup old files best-effort
  if (keep_files_ > 0) {
    std::vector<std::filesystem::directory_entry> matches;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec)) {
      if (ec) break;
      if (!e.is_regular_file()) continue;
      const auto fn = e.path().filename().string();
      if (fn.rfind(stem_ + "_" + session_tag_ + "_", 0) == 0 && e.path().extension() == ext_) {
        matches.push_back(e);
      }
    }
    std::sort(matches.begin(), matches.end(),
              [](const auto& a, const auto& b){ return a.last_write_time() < b.last_write_time(); });
    while (matches.size() > keep_files_) {
      std::error_code ec2;
      std::filesystem::remove(matches.front().path(), ec2);
      matches.erase(matches.begin());
    }
  }

  logger::info() << "[LOG] Binary logging -> " << path.string() << "\n";
  return true;
}

bool RotatingBinaryLog::write_record(const RecordHeader& h, const void* payload, uint16_t n) {
  const uint64_t bytes_to_add = sizeof(RecordHeader) + n;
  if (!rotate_if_needed(bytes_to_add)) return false;
  const bool ok = writer_.write_record(h, payload, n);
  if (ok) bytes_written_ += bytes_to_add;
  return ok;
}

} // namespace utils
