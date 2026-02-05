#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace connection {

class SerialPort {
public:
  SerialPort() = default;
  ~SerialPort();

  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  bool open(const std::string& device, int baud);
  void close();
  bool isOpen() const;

  // blocking read exact N bytes (returns false on error/EOF)
  bool readExact(uint8_t* dst, size_t n);

  // write all bytes (returns false on error)
  bool writeAll(const uint8_t* data, size_t n);

private:
  int fd_ = -1;
};

} // namespace connection
