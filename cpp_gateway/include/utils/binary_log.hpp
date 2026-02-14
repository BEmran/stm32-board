#pragma once
#include <cstdint>
#include <fstream>
#include <string_view>

namespace utils {

// Required record types exactly: STATE, CMD, EVENT
enum class RecordType : uint8_t {
  STATE  = 1,
  CMD = 2,
  EVENT  = 3,
};

#pragma pack(push, 1)
struct FileHeader {
  uint32_t magic{0x47574C42}; // 'BLWG'
  uint16_t ver{1};
  uint16_t reserved{0};
};

struct RecordHeader {
  RecordType type{RecordType::STATE};
  uint8_t    reserved0{0};
  uint16_t   payload_len{0};
  double     epoch_s{0.0};
  double     mono_s{0.0};
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 8, "FileHeader size");
static_assert(sizeof(RecordHeader) == 20, "RecordHeader size");

class BinaryLogWriter {
public:
  bool open(std::string_view path);
  void close();
  bool is_open() const noexcept { return out_.is_open(); }
  bool write_record(const RecordHeader& h, const void* payload, uint16_t n);

private:
  std::ofstream out_;
};

} // namespace utils
