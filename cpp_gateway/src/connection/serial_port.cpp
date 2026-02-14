#include "connection/serial_port.hpp"

#include <cerrno>
#include <cstring>

#ifdef __linux__
  #include <fcntl.h>
  #include <termios.h>
  #include <unistd.h>
#endif

namespace connection {

SerialPort::~SerialPort() noexcept {
  close();
}

bool SerialPort::open(std::string_view device, int baud) {
#ifdef __linux__
  close();

  fd_ = ::open(std::string(device).c_str(), O_RDWR | O_NOCTTY | O_SYNC);
  if (fd_ < 0) return false;

  termios tty{};
  if (tcgetattr(fd_, &tty) != 0) {
    close();
    return false;
  }

  // Configure raw mode
  cfmakeraw(&tty);

  // Baud rate
  auto baud_to_speed = [](int b) -> speed_t {
    switch (b) {
      case 9600: return B9600;
      case 19200: return B19200;
      case 38400: return B38400;
      case 57600: return B57600;
      case 115200: return B115200;
#ifdef B230400
      case 230400: return B230400;
#endif
      default: return B115200;
    }
  };

  const speed_t sp = baud_to_speed(baud);
  cfsetispeed(&tty, sp);
  cfsetospeed(&tty, sp);

  // 8N1
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  // Read behavior: block until at least 1 byte, with timeout in deciseconds.
  tty.c_cc[VMIN]  = 1;
  tty.c_cc[VTIME] = 1; // 100ms

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    close();
    return false;
  }

  return true;
#else
  (void)device; (void)baud;
  return false;
#endif
}

void SerialPort::close() noexcept {
#ifdef __linux__
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
#else
  fd_ = -1;
#endif
}

bool SerialPort::is_open() const noexcept {
  return fd_ >= 0;
}

bool SerialPort::read_exact(uint8_t* dst, size_t n) {
#ifdef __linux__
  size_t got = 0;
  while (got < n) {
    const ssize_t r = ::read(fd_, dst + got, n - got);
    if (r > 0) {
      got += static_cast<size_t>(r);
      continue;
    }
    if (r == 0) return false; // EOF
    if (errno == EINTR) continue;
    return false;
  }
  return true;
#else
  (void)dst; (void)n;
  return false;
#endif
}

bool SerialPort::write_all(const uint8_t* data, size_t n) {
#ifdef __linux__
  size_t sent = 0;
  while (sent < n) {
    const ssize_t w = ::write(fd_, data + sent, n - sent);
    if (w > 0) {
      sent += static_cast<size_t>(w);
      continue;
    }
    if (w == 0) return false;
    if (errno == EINTR) continue;
    return false;
  }
  return true;
#else
  (void)data; (void)n;
  return false;
#endif
}

} // namespace connection
