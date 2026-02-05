#pragma once
#include "rosmaster/protocol.hpp"
#include "connection/serial_port.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <string>
#include <string_view>
#include <array>

namespace rosmaster {

struct ImuRaw {
  float ax{}, ay{}, az{};
  float gx{}, gy{}, gz{};
  float mx{}, my{}, mz{};
};

struct Attitude {
  float roll{}, pitch{}, yaw{}; // radians (same as Python stored)
};

struct Encoder {
  int32_t e1{}, e2{}, e3{}, e4{};
};

struct SpeedBattery {
  float vx{}, vy{}, vz{};
  float battery_voltage{}; // volts
};

struct State {
  uint32_t last_seq = 0;      // not from firmware; for your external use if needed
  ImuRaw imu{};
  Attitude ang{};
  Encoder enc{};
  SpeedBattery spd{};
  float version = 0.0f;       // e.g., 1.1
};

class Rosmaster {
public:
  Rosmaster() = default;
  ~Rosmaster();

  bool connect(const std::string& device = "/dev/ttyUSB0", int baud = 115200, bool debug=false);
  void disconnect();

  bool startReceiveThread();
  void stopReceiveThread();

  // Commands (subset)
  bool setAutoReport(bool enable, bool forever=false);
  bool setBeep(int on_time_ms);
  bool setMotor(int s1, int s2, int s3, int s4);
  bool requestVersion(); // sends request; parsed reply fills state.version

  // Thread-safe snapshot of latest state
  State getState() const;

private:
  void rxLoop();
  void parsePayload(uint8_t ext_type, const uint8_t* data, size_t len);

  uint8_t limitMotorValue(int v) const;

  bool sendFrame(uint8_t func, const uint8_t* payload, size_t payload_len, bool fixed_len_5=false);
  bool requestData(uint8_t function, uint8_t param=0);

private:
  mutable std::mutex mtx_;
  State state_{};

  SerialPort ser_;
  bool debug_ = false;

  std::atomic<bool> running_{false};
  std::thread rx_thread_;
};

} // namespace rosmaster
