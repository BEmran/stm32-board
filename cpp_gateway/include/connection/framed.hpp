#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <arpa/inet.h>

namespace connection {

// Framed binary protocol over TCP (stream-safe).
// Header is always 4 bytes, with payload length in NETWORK BYTE ORDER (big-endian).
#pragma pack(push, 1)
struct MsgHdr {
  uint8_t  type;   // 1=STATE, 2=CMD
  uint8_t  ver;    // protocol version (currently 1)
  uint16_t len_be; // payload length (bytes), network byte order
};
#pragma pack(pop)

static_assert(sizeof(MsgHdr) == 4, "MsgHdr must be 4 bytes");

inline constexpr uint8_t MSG_VER   = 1;
inline constexpr uint8_t MSG_STATE = 1;
inline constexpr uint8_t MSG_CMD   = 2;

inline MsgHdr make_hdr(uint8_t type, uint16_t payload_len) {
  MsgHdr h{};
  h.type = type;
  h.ver  = MSG_VER;
  h.len_be = htons(payload_len);
  return h;
}

inline uint16_t hdr_len(const MsgHdr& h) {
  return ntohs(h.len_be);
}

// Simple stream decoder for MsgHdr+payload frames.
class FrameRx {
public:
  // Append bytes to internal buffer
  void push_bytes(const uint8_t* data, size_t n) {
    buf_.insert(buf_.end(), data, data + n);
  }

  // Try to pop one complete frame. Returns true if a frame was produced.
  bool pop(uint8_t& out_type, std::vector<uint8_t>& out_payload) {
    if (buf_.size() < sizeof(MsgHdr)) return false;

    MsgHdr h{};
    std::memcpy(&h, buf_.data(), sizeof(h));

    if (h.ver != MSG_VER) {
      // resync: drop one byte and try again next time
      buf_.erase(buf_.begin());
      return false;
    }

    const uint16_t len = hdr_len(h);
    const size_t total = sizeof(MsgHdr) + static_cast<size_t>(len);

    // sanity limit to avoid runaway memory on corrupt streams
    if (len > 2048) {
      buf_.erase(buf_.begin());
      return false;
    }

    if (buf_.size() < total) return false;

    out_type = h.type;
    out_payload.assign(buf_.begin() + sizeof(MsgHdr), buf_.begin() + total);
    buf_.erase(buf_.begin(), buf_.begin() + total);
    return true;
  }

  void clear() { buf_.clear(); }

private:
  std::vector<uint8_t> buf_;
};

} // namespace connection
