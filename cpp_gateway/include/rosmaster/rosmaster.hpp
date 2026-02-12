#pragma once
#include "rosmaster/protocol.hpp"
#include "core/basic.hpp"
#include "connection/serial_port.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
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

  [[nodiscard]] bool start();   // start RX thread
  void stop();    // stop RX thread

  // ---- Basic control ----
  [[nodiscard]] bool set_auto_report_state(bool enable, bool forever=false);
  [[nodiscard]] bool set_beep(int on_time_ms);

  [[nodiscard]] bool set_pwm_servo(uint8_t servo_id, int angle_deg); // servo_id 1..4
  [[nodiscard]] bool set_pwm_servo_all(int a1,int a2,int a3,int a4); // angle 0..180 or 255 "ignore"

  [[nodiscard]] bool set_colorful_lamps(uint8_t led_id, uint8_t r, uint8_t g, uint8_t b);
  [[nodiscard]] bool set_colorful_effect(uint8_t effect, uint8_t speed=255, uint8_t parm=255);

  [[nodiscard]] bool set_motor(int s1, int s2, int s3, int s4); // -100..100, 127 keep
  [[nodiscard]] bool set_pid_param(float kp, float ki, float kd, bool forever=false);

  // ---- Reset ----
  [[nodiscard]] bool reset_flash_value();
  void clear_auto_report_data();

  // ---- Getters (fast, no request) ----
  [[nodiscard]] core::Vec3d get_accelerometer_data() const;
  [[nodiscard]] core::Vec3d get_gyroscope_data() const;
  [[nodiscard]] core::Vec3d get_magnetometer_data() const;
  [[nodiscard]] core::Encoders get_motor_encoder() const;
  [[nodiscard]] float get_battery_voltage() const;

  // attitude: degrees by default like python
  [[nodiscard]] core::Angles get_imu_attitude_data() const;

  // ---- Request/response getters ----
  [[nodiscard]] std::pair<int,int> get_uart_servo_value(uint8_t servo_id); // returns {id, pulse} or {-1,-1}
  [[nodiscard]] int get_uart_servo_angle(uint8_t s_id);                    // returns angle or -1

  [[nodiscard]] float get_version();                                       // version or -1

  // Snapshot (full internal state)
  [[nodiscard]] core::States get_state() const;
  [[nodiscard]] bool apply_actions(const core::Actions& actions);

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
  core::States st_{};
  core::Version version_{};

  // signal that "something of ext_type arrived"
  mutable std::mutex ev_mtx_;
  std::condition_variable ev_cv_;
  std::array<uint32_t, 256> ev_count_{};
};

} // namespace rosmaster
