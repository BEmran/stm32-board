#pragma once
#include "rosmaster/protocol.hpp"
#include "core/basic.hpp"
#include "connection/serial_port.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <array>

namespace rosmaster {

struct Config {
  std::string device{"/dev/ttyUSB0"};
  int baud{115200};
  std::chrono::microseconds cmd_delay{2000};
  bool debug{false};
};

// ---- Rosmaster interface ----
class Rosmaster {
public:
  Rosmaster() = default;
  explicit Rosmaster(const Config& cfg);
  ~Rosmaster();

  bool connect(const Config& cfg);
  void disconnect();

  bool start();   // start RX thread
  void stop();    // stop RX thread

  // ---- Basic control ----
  bool set_auto_report_state(bool enable, bool forever=false);
  bool set_beep(int on_time_ms);

  bool set_pwm_servo(uint8_t servo_id, int angle_deg); // servo_id 1..4
  bool set_pwm_servo_all(int a1,int a2,int a3,int a4); // angle 0..180 or 255 "ignore"

  bool set_colorful_lamps(uint8_t led_id, uint8_t r, uint8_t g, uint8_t b);
  bool set_colorful_effect(uint8_t effect, uint8_t speed=255, uint8_t parm=255);

  bool set_motor(int s1, int s2, int s3, int s4); // -100..100, 127 keep
  bool set_pid_param(float kp, float ki, float kd, bool forever=false);

  // ---- Reset ----
  bool reset_flash_value();
  void clear_auto_report_data();

  // ---- Getters (fast, no request) ----
  core::Vec3d get_accelerometer_data() const;
  core::Vec3d get_gyroscope_data() const;
  core::Vec3d get_magnetometer_data() const;
  core::Encoders get_motor_encoder() const;
  float get_battery_voltage() const;

  // attitude: degrees by default like python
  core::Angles get_imu_attitude_data() const;

  // ---- Request/response getters ----
  std::pair<int,int> get_uart_servo_value(uint8_t servo_id); // returns {id, pulse} or {-1,-1}
  int get_uart_servo_angle(uint8_t s_id);                    // returns angle or -1

  float get_version();                                       // version or -1

  // Snapshot (full internal state)
  core::State get_state() const;

private:
  // internals
  void rx_loop();
  void parse_payload(uint8_t ext_type, const uint8_t* data, size_t len);

  bool request_data(uint8_t function, uint8_t param=0);

  // frame writers
  bool send_fixed5(uint8_t func, uint8_t p0, uint8_t p1);
  bool send_var(uint8_t func, const std::vector<uint8_t>& payload);

  // helpers
  static int  clamp_int(int v, int lo, int hi);
  static int8_t limit_motor_value(int v); // python behavior (127 keep)

  // wait/notify for request-response parsing
  bool wait_for(uint8_t ext_type, std::chrono::milliseconds timeout);

private:
  Config cfg_{};
  connection::SerialPort ser_;

  std::atomic<bool> running_{false};
  std::thread rx_thread_;

  mutable std::mutex mtx_;
  core::State st_{};
  core::Version version_{};

  // signal that "something of ext_type arrived"
  mutable std::mutex ev_mtx_;
  std::condition_variable ev_cv_;
  std::array<uint32_t, 256> ev_count_{};
};

} // namespace rosmaster
