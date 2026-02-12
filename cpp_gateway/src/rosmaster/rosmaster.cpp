#include "rosmaster/rosmaster.hpp"
#include "utils/cast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

namespace rosmaster {

constexpr float GYRO_RATIO {1.0f / 3754.9f};
constexpr float ACCEL_RATIO{1.0f / 1671.84f};
constexpr float MAG_RATIO  {1.0f};
constexpr float Milli_RATIO  {1.0f / 1000.0f};

namespace {
  [[nodiscard]] core::Vec3d parse_vec3d(const uint8_t* d) noexcept {
    core::Vec3d vec;
    vec.x = utils::le_i16(d+0);
    vec.y = utils::le_i16(d+2);
    vec.z = utils::le_i16(d+4);
    return vec;
  }
}

Rosmaster::Rosmaster(const Config& cfg) { connect(cfg); }

Rosmaster::~Rosmaster() {
  stop();
  disconnect();
}

bool Rosmaster::connect(const Config& cfg) {
  cfg_ = cfg;
  if (!ser_.open(cfg_.device, cfg_.baud)) return false;
  if (cfg_.debug) std::cout << "Rosmaster Serial Opened! Baudrate=" << cfg_.baud << "\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  return true;
}

void Rosmaster::disconnect() { ser_.close(); }

bool Rosmaster::start() {
  if (running_.load()) return true;
  if (!ser_.isOpen()) return false;
  running_.store(true);
  rx_thread_ = std::thread(&Rosmaster::rx_loop, this);
  std::this_thread::sleep_for(std::chrono::milliseconds(50)); // like python create_receive_threading
  return true;
}

void Rosmaster::stop() {
  running_.store(false);
  if (rx_thread_.joinable()) rx_thread_.join();
}

core::States Rosmaster::get_state() const {
  std::scoped_lock lk(mtx_);
  return st_;
}

bool Rosmaster::apply_actions(const core::Actions& actions) {
  return set_beep(actions.beep_ms) &&
         set_motor(actions.motors.m1, actions.motors.m2, actions.motors.m3, actions.motors.m4);
}

int Rosmaster::clamp_int(int v, int lo, int hi) {
  return std::clamp(v, lo, hi);
}

int8_t Rosmaster::limit_motor_value(int v) {
  if (v == 127) return 127;
  if (v > 100) return 100;
  if (v < -100) return -100;
  return static_cast<int8_t>(v);
}

// ---------------- Frame building ----------------
// fixed 0x05 frames: [FF, DEVICE_ID, 0x05, func, p0, p1, checksum]
bool Rosmaster::send_fixed5(uint8_t func, uint8_t p0, uint8_t p1) {
  std::array<uint8_t, 7> cmd{HEAD, DEVICE_ID, 0x05, func, p0, p1, 0};
  uint8_t sum = COMPLEMENT;
  for (size_t i = 0; i + 1 < cmd.size(); i++) {
    sum = static_cast<uint8_t>(sum + cmd[i]);
  }
  cmd.back() = sum;
  const bool ok = ser_.writeAll(cmd);
  std::this_thread::sleep_for(cfg_.cmd_delay);
  return ok;
}

// variable frames: cmd[2] = len(cmd)-1 before checksum, checksum = sum(cmd, COMPLEMENT)&0xFF
bool Rosmaster::send_var(uint8_t func, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> cmd;
  cmd.reserve(4 + payload.size() + 1);
  cmd.push_back(HEAD);
  cmd.push_back(DEVICE_ID);
  cmd.push_back(0x00); // placeholder len-1
  cmd.push_back(func);
  cmd.insert(cmd.end(), payload.begin(), payload.end());

  cmd[2] = static_cast<uint8_t>(cmd.size() - 1);

  uint8_t sum = COMPLEMENT;
  for (auto b : cmd) sum = static_cast<uint8_t>(sum + b);
  cmd.push_back(sum);

  const bool ok = ser_.writeAll(cmd.data(), cmd.size());
  std::this_thread::sleep_for(cfg_.cmd_delay);
  return ok;
}

bool Rosmaster::request_data(uint8_t function, uint8_t param) {
  // python __request_data: fixed 0x05 on FUNC_REQUEST_DATA with (function,param) :contentReference[oaicite:2]{index=2}
  return send_fixed5(FUNC_REQUEST_DATA, function, param);
}

// wait for response event of ext_type
bool Rosmaster::wait_for(uint8_t ext_type, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lk(ev_mtx_);
  uint32_t start = ev_count_[ext_type];
  return ev_cv_.wait_for(lk, timeout, [&] {
    return ev_count_[ext_type] != start;
  });
}

// ---------------- RX parsing ----------------
void Rosmaster::rx_loop() {
  // mirrors python __receive_data framing:
  // head1=0xFF, head2=DEVICE_ID-1, ext_len, ext_type, (ext_len-2) data bytes incl checksum :contentReference[oaicite:3]{index=3}
  while (running_.load()) {
    uint8_t h1{};
    if (!ser_.readExact(&h1, 1)) continue;
    if (h1 != HEAD) continue;

    uint8_t h2{};
    if (!ser_.readExact(&h2, 1)) continue;
    if (h2 != static_cast<uint8_t>(DEVICE_ID - 1)) continue;

    uint8_t ext_len{};
    uint8_t ext_type{};
    if (!ser_.readExact(&ext_len, 1)) continue;
    if (!ser_.readExact(&ext_type, 1)) continue;

    const int data_len = static_cast<int>(ext_len) - 2;
    if (data_len <= 0 || data_len > 200) continue;

    std::vector<uint8_t> buf(static_cast<size_t>(data_len));
    if (!ser_.readExact(buf.data(), buf.size())) continue;

    const uint8_t rx_check = buf.back();
    uint32_t sum = static_cast<uint32_t>(ext_len) + static_cast<uint32_t>(ext_type);
    for (size_t i = 0; i + 1 < buf.size(); i++) sum += buf[i];
    if (static_cast<uint8_t>(sum & 0xFF) != rx_check) {
      if (cfg_.debug) std::cout << "checksum error type=" << int(ext_type) << "\n";
      continue;
    }

    parse_payload(ext_type, buf.data(), buf.size() - 1);

    // notify waiters
    {
      std::lock_guard<std::mutex> lk(ev_mtx_);
      ev_count_[ext_type]++;   // ext_type is uint8_t
    }
    ev_cv_.notify_all();
  }
}


void Rosmaster::parse_payload(uint8_t ext_type, const uint8_t* d, size_t n) {
  std::scoped_lock lk(mtx_);

  if (ext_type == FUNC_REPORT_SPEED && n >= 7) {
    st_.battery_voltage = (float)(d[6]) / 10.0f;
    return;
  }

  if (ext_type == FUNC_REPORT_MPU_RAW && n >= 18) {
    st_.imu.gyro = core::scale_vec3d(core::rearrange_gyro(parse_vec3d(d)), GYRO_RATIO);
    st_.imu.acc = core::scale_vec3d(parse_vec3d(d+6), ACCEL_RATIO);
    st_.imu.mag = core::scale_vec3d(parse_vec3d(d+12), MAG_RATIO);
    return;
  }

  if (ext_type == FUNC_REPORT_ICM_RAW && n >= 18) {
    st_.imu.gyro = core::scale_vec3d(parse_vec3d(d), Milli_RATIO);
    st_.imu.acc = core::scale_vec3d(parse_vec3d(d+6), Milli_RATIO);
    st_.imu.mag = core::scale_vec3d(parse_vec3d(d+12), Milli_RATIO);
    return;
  }

  if (ext_type == FUNC_REPORT_IMU_ATT && n >= 6) {
    st_.ang.roll  = utils::le_i16(d+0) / 10000.0f;
    st_.ang.pitch = utils::le_i16(d+2) / 10000.0f;
    st_.ang.yaw   = utils::le_i16(d+4) / 10000.0f;
    return;
  }

  if (ext_type == FUNC_REPORT_ENCODER && n >= 16) {
    st_.enc.e1 = utils::le_i32(d+0);
    st_.enc.e2 = utils::le_i32(d+4);
    st_.enc.e3 = utils::le_i32(d+8);
    st_.enc.e4 = utils::le_i32(d+12);
    return;
  }

  if (ext_type == FUNC_VERSION && n >= 2) {
    version_.high = d[0];
    version_.low = d[1];
    version_.version = (float)version_.high + (float)version_.low / 10.0f;
    return;
  }
}

// ---------------- Public API (full) ----------------
bool Rosmaster::set_auto_report_state(bool enable, bool forever) {
  const uint8_t state1 = enable ? 1 : 0;
  const uint8_t state2 = forever ? 0x5F : 0;
  return send_fixed5(FUNC_AUTO_REPORT, state1, state2);
}

bool Rosmaster::set_beep(int on_time_ms) {
  if (on_time_ms < 0) return false;
  const int16_t v = static_cast<int16_t>(on_time_ms);
  return send_fixed5(FUNC_BEEP, uint8_t(v & 0xFF), uint8_t((v >> 8) & 0xFF));
}

bool Rosmaster::set_pwm_servo(uint8_t servo_id, int angle_deg) {
  if (servo_id < 1 || servo_id > 4) return false;
  angle_deg = clamp_int(angle_deg, 0, 180);
  return send_var(FUNC_PWM_SERVO, {servo_id, (uint8_t)angle_deg});
}

bool Rosmaster::set_pwm_servo_all(int a1,int a2,int a3,int a4) {
  auto fix = [](int a){ return (a < 0 || a > 180) ? 255 : a; };
  return send_var(FUNC_PWM_SERVO_ALL, {(uint8_t)fix(a1),(uint8_t)fix(a2),(uint8_t)fix(a3),(uint8_t)fix(a4)});
}

bool Rosmaster::set_colorful_lamps(uint8_t led_id, uint8_t r, uint8_t g, uint8_t b) {
  return send_var(FUNC_RGB, {led_id, r, g, b});
}

bool Rosmaster::set_colorful_effect(uint8_t effect, uint8_t speed, uint8_t parm) {
  return send_var(FUNC_RGB_EFFECT, {effect, speed, parm});
}

bool Rosmaster::set_motor(int s1, int s2, int s3, int s4) {
  const int8_t a = limit_motor_value(s1);
  const int8_t b = limit_motor_value(s2);
  const int8_t c = limit_motor_value(s3);
  const int8_t d = limit_motor_value(s4);
  return send_var(FUNC_MOTOR, {(uint8_t)a,(uint8_t)b,(uint8_t)c,(uint8_t)d});
}

// ---- Reset ----
bool Rosmaster::reset_flash_value() {
  const bool ok = send_var(FUNC_RESET_FLASH, {0x5F});
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return ok;
}

void Rosmaster::clear_auto_report_data() {
  std::scoped_lock lk(mtx_);
  st_.imu = {};
  st_.ang = {};
  st_.enc = {};
  st_.battery_voltage = 0.0f;
}

// ---- Fast getters ----
core::Vec3d Rosmaster::get_accelerometer_data() const {
  std::scoped_lock lk(mtx_);
  return st_.imu.acc;
}

core::Vec3d Rosmaster::get_gyroscope_data() const {
  std::scoped_lock lk(mtx_);
  return st_.imu.gyro;
}

core::Vec3d Rosmaster::get_magnetometer_data() const {
  std::scoped_lock lk(mtx_);
  return st_.imu.mag;
}

float Rosmaster::get_battery_voltage() const {
  std::scoped_lock lk(mtx_);
  return st_.battery_voltage;
}

core::Encoders Rosmaster::get_motor_encoder() const {
  std::scoped_lock lk(mtx_);
  return {st_.enc.e1, st_.enc.e2, st_.enc.e3, st_.enc.e4};
}

core::Angles Rosmaster::get_imu_attitude_data() const {
  std::scoped_lock lk(mtx_);
  return st_.ang;
}

// ---- Request/response getters ----
float Rosmaster::get_version() {
  {
    std::scoped_lock lk(mtx_);
    if (version_.high != 0) return version_.version;
    version_.high = version_.low = 0;
    version_.version = 0;
  }

  request_data(FUNC_VERSION, 0);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    float v=0.0f; uint8_t h=0;
    {
      std::scoped_lock lk(mtx_);
      h = version_.high;
      v = version_.version;
    }
    if (h != 0) return v;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return -1.0f;
}

} // namespace rosmaster
