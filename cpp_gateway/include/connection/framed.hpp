#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace connection
{

// Framed binary protocol over TCP (stream-safe).
// Header is always 3 bytes, with payload length as a single byte (0..255).
#pragma pack(push, 1)
  struct MsgHdr
  {
    uint8_t type; // 1=STATE, 2=CMD, 3=SETPOINT, 4=CONFIG
    uint8_t ver;  // protocol version (currently 1)
    uint8_t len;  // payload length (bytes)
  };
#pragma pack(pop)

  static_assert(sizeof(MsgHdr) == 3, "MsgHdr must be 3 bytes");

  constexpr uint8_t MSG_VER = 1;
  constexpr uint8_t MSG_STATE = 1;
  constexpr uint8_t MSG_CMD = 2;       // legacy
  constexpr uint8_t MSG_SETPOINT = 3;  // new
  constexpr uint8_t MSG_CONFIG = 4;    // new

  inline MsgHdr make_hdr(uint8_t type, uint8_t payload_len)
  {
    MsgHdr h{};
    h.type = type;
    h.ver = MSG_VER;
    h.len = payload_len;
    return h;
  }

  inline uint8_t hdr_len(const MsgHdr &h)
  {
    return h.len;
  }

  // Simple stream decoder for MsgHdr+payload frames.
  class FrameRx
  {
  public:
    // Append bytes to internal buffer
    void push_bytes(const uint8_t *data, size_t n)
    {
      buf_.insert(buf_.end(), data, data + n);
    }

    // Try to pop one complete frame. Returns true if a frame was produced.
    bool pop(uint8_t &out_type, std::vector<uint8_t> &out_payload)
    {
      if (buf_.size() < sizeof(MsgHdr))
        return false;

      MsgHdr h{};
      std::memcpy(&h, buf_.data(), sizeof(h));

      if (h.ver != MSG_VER)
      {
        // resync: drop one byte and try again next time
        buf_.erase(buf_.begin());
        return false;
      }

      constexpr size_t kMaxPayload = 255;
      const size_t len = static_cast<size_t>(hdr_len(h));
      const size_t total = sizeof(MsgHdr) + len;

      // sanity limit to avoid runaway memory on corrupt streams
      if (len > kMaxPayload)
      {
        buf_.erase(buf_.begin());
        return false;
      }

      if (buf_.size() < total)
        return false;

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
