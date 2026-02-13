#include "utils/binary_log.hpp"
#include <filesystem>

namespace utils {

bool BinaryLogWriter::open(std::string_view path) {
  close();

  try {
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
  } catch (...) {}

  out_.open(std::string(path), std::ios::binary | std::ios::out);
  if (!out_.is_open()) return false;

  FileHeader fh{};
  out_.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  return static_cast<bool>(out_);
}

void BinaryLogWriter::close() {
  if (out_.is_open()) {
    out_.flush();
    out_.close();
  }
}

bool BinaryLogWriter::write_record(const RecordHeader& h, const void* payload, uint16_t n) {
  if (!out_.is_open()) return false;

  RecordHeader hh = h;
  hh.payload_len = n;

  out_.write(reinterpret_cast<const char*>(&hh), sizeof(hh));
  if (n && payload) out_.write(reinterpret_cast<const char*>(payload), n);
  return static_cast<bool>(out_);
}

} // namespace utils
