#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace connection
{

  class SerialPort
  {
  public:
    SerialPort() = default;
    ~SerialPort() noexcept;

    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;

     bool open(std::string_view device, int baud);
    void close() noexcept;
     bool isOpen() const noexcept;

    // blocking read exact N bytes (returns false on error/EOF)
     bool readExact(uint8_t *dst, size_t n);
     bool readExact(std::span<uint8_t> dst)
    {
      return readExact(dst.data(), dst.size());
    }

    // write all bytes (returns false on error)
     bool writeAll(const uint8_t *data, size_t n);
     bool writeAll(std::span<const uint8_t> data)
    {
      return writeAll(data.data(), data.size());
    }

  private:
    int fd_ = -1;
  };

} // namespace connection
