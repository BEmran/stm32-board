#pragma once
#include "utils/binary_log.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace utils {

/**
 * @brief Simple size-based rotating binary log.
 *
 * Creates files like:
 *   <base_dir>/<base_stem>_YYYYmmdd_HHMMSS_<index>.bin
 *
 * Rotation is based on max_bytes. Old files beyond keep_files are deleted
 * best-effort.
 */
class RotatingBinaryLog {
public:
  bool open(const std::string& base_path, uint64_t max_bytes, uint32_t keep_files);
  void close();

  bool is_open() const noexcept { return writer_.is_open(); }

  bool write_record(const RecordHeader& h, const void* payload, uint16_t n);

private:
  bool rotate_if_needed(uint64_t bytes_to_add);
  bool open_new_file();

  std::filesystem::path base_path_{};
  std::filesystem::path dir_{};
  std::string stem_{};
  std::string ext_{".bin"};
  uint64_t max_bytes_{0};
  uint32_t keep_files_{0};
  uint32_t index_{0};
  uint64_t bytes_written_{0};
  std::string session_tag_{};

  BinaryLogWriter writer_;
};

} // namespace utils
