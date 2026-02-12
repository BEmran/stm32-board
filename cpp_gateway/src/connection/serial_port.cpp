#include "connection/serial_port.hpp"

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace connection {

static speed_t baudToConst(int baud) {
  switch (baud) {
    case 115200: return B115200;
    case 57600:  return B57600;
    case 38400:  return B38400;
    case 19200:  return B19200;
    case 9600:   return B9600;
    default:     return B115200;
  }
}

SerialPort::~SerialPort() noexcept { close(); }

bool SerialPort::open(std::string_view device, int baud) {
  close();
  const std::string dev(device);
  fd_ = ::open(dev.c_str(), O_RDWR | O_NOCTTY);
  if (fd_ < 0) return false;

  termios tty{};
  if (tcgetattr(fd_, &tty) != 0) { close(); return false; }
  cfmakeraw(&tty);

  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);

  // responsive blocking reads
  tty.c_cc[VMIN]  = 1;
  tty.c_cc[VTIME] = 1; // 0.1s

  const speed_t spd = baudToConst(baud);
  cfsetispeed(&tty, spd);
  cfsetospeed(&tty, spd);

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) { close(); return false; }
  tcflush(fd_, TCIFLUSH);
  return true;
}

void SerialPort::close() noexcept {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool SerialPort::isOpen() const noexcept { return fd_ >= 0; }

bool SerialPort::readExact(uint8_t* dst, size_t n) {
  size_t got = 0;
  while (got < n) {
    const ssize_t r = ::read(fd_, dst + got, n - got);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

bool SerialPort::writeAll(const uint8_t* data, size_t n) {
  size_t sent = 0;
  while (sent < n) {
    const ssize_t w = ::write(fd_, data + sent, n - sent);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) return false;
    sent += static_cast<size_t>(w);
  }
  return true;
}

} // namespace connection
