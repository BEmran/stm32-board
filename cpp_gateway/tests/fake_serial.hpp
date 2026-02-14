#pragma once
#include "connection/serial_port.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string_view>
#include <vector>

namespace tests {

/**
 * @brief Simple thread-safe fake serial port for unit tests.
 *
 * - read_exact() consumes from an RX queue.
 * - write_all() appends to a TX vector.
 */
class FakeSerialPort final : public connection::ISerialPort {
public:
  bool open(std::string_view /*device*/, int /*baud*/) override {
    open_ = true;
    return true;
  }

  void close() noexcept override { open_ = false; }

  bool is_open() const noexcept override { return open_; }

  bool read_exact(uint8_t* dst, size_t n) override {
    std::unique_lock<std::mutex> lk(m_);
    if (!open_) return false;
    if (rx_.size() < n) return false;
    for (size_t i=0;i<n;i++) {
      dst[i] = rx_.front();
      rx_.pop_front();
    }
    return true;
  }

  bool write_all(const uint8_t* data, size_t n) override {
    std::unique_lock<std::mutex> lk(m_);
    if (!open_) return false;
    tx_.insert(tx_.end(), data, data+n);
    return true;
  }

  void push_rx(const std::vector<uint8_t>& bytes) {
    std::unique_lock<std::mutex> lk(m_);
    rx_.insert(rx_.end(), bytes.begin(), bytes.end());
  }

  std::vector<uint8_t> take_tx() {
    std::unique_lock<std::mutex> lk(m_);
    auto out = tx_;
    tx_.clear();
    return out;
  }

private:
  mutable std::mutex m_;
  bool open_{false};
  std::deque<uint8_t> rx_;
  std::vector<uint8_t> tx_;
};

} // namespace tests
