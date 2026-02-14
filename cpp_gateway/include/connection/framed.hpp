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
    uint8_t type; // 1=STATE, 2=CMD (legacy), 3=SETPOINT, 4=CONFIG
    uint8_t ver;  // protocol version (currently 1)
    uint8_t len;  // payload length (bytes)
  };
#pragma pack(pop)

  static_assert(sizeof(MsgHdr) == 3, "MsgHdr must be 3 bytes");

  constexpr uint8_t MSG_VER = 1;
  constexpr uint8_t MSG_STATE = 1;
  constexpr uint8_t MSG_CMD = 2;       // legacy
  constexpr uint8_t MSG_SETPOINT = 3;  // latest-wins
  constexpr uint8_t MSG_CONFIG = 4;    // latest-wins
  constexpr uint8_t MSG_STATS_REQ  = 5; // request stats (len=0)
  constexpr uint8_t MSG_STATS_RESP = 6; // response stats (fixed payload)

  inline bool is_known_type(uint8_t t) noexcept
  {
    return (t == MSG_STATE) || (t == MSG_CMD) || (t == MSG_SETPOINT) || (t == MSG_CONFIG) || (t == MSG_STATS_REQ) || (t == MSG_STATS_RESP);
  }

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

  /**
   * @brief Stream decoder for MsgHdr+payload frames.
   *
   * This implementation avoids O(n) vector::erase() on every frame by keeping
   * a read cursor and compacting occasionally.
   *
   * It also protects against runaway memory growth by enforcing a buffer cap.
   */
  class FrameRx
  {
  public:
    static constexpr size_t kMaxPayload = 255;
    static constexpr size_t kMaxBufferBytes = 64 * 1024; // hard cap against junk streams
    static constexpr size_t kCompactThreshold = 4096;    // compact when read_pos exceeds this

    void push_bytes(const uint8_t *data, size_t n)
    {
      if (n == 0) return;

      // Enforce hard cap: if peer floods without valid frames, reset buffer.
      if (available_bytes() + n > kMaxBufferBytes)
      {
        clear();
        // If still too large, keep only the tail that fits.
        if (n > kMaxBufferBytes)
        {
          data += (n - kMaxBufferBytes);
          n = kMaxBufferBytes;
        }
      }

      buf_.insert(buf_.end(), data, data + n);
    }

    bool pop(uint8_t &out_type, std::vector<uint8_t> &out_payload)
    {
      if (available_bytes() < sizeof(MsgHdr))
        return false;

      MsgHdr h{};
      std::memcpy(&h, buf_.data() + read_pos_, sizeof(h));

      // quick sanity: version + known type
      if (h.ver != MSG_VER || !is_known_type(h.type))
      {
        // resync: drop one byte
        ++read_pos_;
        maybe_compact();
        return false;
      }

      const size_t len = static_cast<size_t>(hdr_len(h));
      const size_t total = sizeof(MsgHdr) + len;

      if (len > kMaxPayload)
      {
        ++read_pos_;
        maybe_compact();
        return false;
      }

      // optional semantic sanity: some types must have payload
      if (len == 0 && (h.type == MSG_CMD || h.type == MSG_SETPOINT || h.type == MSG_CONFIG || h.type == MSG_STATS_RESP))
      {
        ++read_pos_;
        maybe_compact();
        return false;
      }

      if (available_bytes() < total)
        return false;

      out_type = h.type;
      out_payload.assign(buf_.begin() + static_cast<std::ptrdiff_t>(read_pos_ + sizeof(MsgHdr)),
                         buf_.begin() + static_cast<std::ptrdiff_t>(read_pos_ + total));

      read_pos_ += total;
      maybe_compact();
      return true;
    }

    void clear()
    {
      buf_.clear();
      read_pos_ = 0;
    }

    size_t available_bytes() const noexcept
    {
      return (read_pos_ <= buf_.size()) ? (buf_.size() - read_pos_) : 0;
    }

  private:
    void maybe_compact()
    {
      // Compact when we've consumed enough or buffer is fully consumed.
      if (read_pos_ == buf_.size())
      {
        clear();
        return;
      }

      if (read_pos_ >= kCompactThreshold && read_pos_ > (buf_.size() / 2))
      {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(read_pos_));
        read_pos_ = 0;
      }
    }

    std::vector<uint8_t> buf_;
    size_t read_pos_{0};
  };

} // namespace connection
