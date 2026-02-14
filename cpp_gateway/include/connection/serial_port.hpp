#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace connection {

/**
 * @brief Minimal serial port abstraction.
 *
 * This interface allows injecting a fake serial backend for tests/replay without changing
 * the rest of the gateway logic.
 */
class ISerialPort {
public:
  virtual ~ISerialPort() noexcept = default;

  virtual bool open(std::string_view device, int baud) = 0;
  virtual void close() noexcept = 0;
  virtual bool is_open() const noexcept = 0;

  virtual bool read_exact(uint8_t* dst, size_t n) = 0;
  virtual bool write_all(const uint8_t* data, size_t n) = 0;

  bool read_exact(std::span<uint8_t> dst) { return read_exact(dst.data(), dst.size()); }
  bool write_all(std::span<const uint8_t> data) { return write_all(data.data(), data.size()); }
};

/**
 * @brief POSIX serial implementation (Linux).
 */
class SerialPort final : public ISerialPort {
public:
  SerialPort() = default;
  ~SerialPort() noexcept override;

  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  bool open(std::string_view device, int baud) override;
  void close() noexcept override;
  bool is_open() const noexcept override;

  bool read_exact(uint8_t* dst, size_t n) override;
  bool write_all(const uint8_t* data, size_t n) override;

  // Backwards-compatible wrappers (old names)
  bool isOpen() const noexcept { return is_open(); }
  bool readExact(uint8_t* dst, size_t n) { return read_exact(dst, n); }
  bool writeAll(const uint8_t* data, size_t n) { return write_all(data, n); }

private:
  int fd_{-1};
};

using SerialPortPtr = std::unique_ptr<ISerialPort>;

} // namespace connection
