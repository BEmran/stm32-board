#include "rosmaster/rosmaster.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace rosmaster {

Rosmaster::~Rosmaster() {
  stopReceiveThread();
  disconnect();
}

bool Rosmaster::connect(const std::string& device, int baud, bool debug) {
  debug_ = debug;
  if (!ser_.open(device, baud)) return false;

  if (debug_) std::cout << "Rosmaster Serial Opened! Baudrate=" << baud << "\n";

  // Python calls set_uart_servo_torque(1) here; we skip (not needed for your use case).
  // You can add it later the same way as other commands.

  return true;
}

void Rosmaster::disconnect() { ser_.close(); }

bool Rosmaster::startReceiveThread() {
  if (running_.load()) return true;
  if (!ser_.isOpen()) return false;
  running_.store(true);
  rx_thread_ = std::thread(&Rosmaster::rxLoop, this);
  return true;
}

void Rosmaster::stopReceiveThread() {
  running_.store(false);
  if (rx_thread_.joinable()) rx_thread_.join();
}

State Rosmaster::getState() const {
  std::scoped_lock lk(mtx_);
  return state_;
}

uint8_t Rosmaster::limitMotorValue(int v) const {
  // Python: keep 127 as "no change", clamp otherwise to [-100,100]
  if (v == 127) return 127;
  if (v > 100) return 100;
  if (v < -100) return static_cast<uint8_t>(static_cast<int8_t>(-100));
  return static_cast<uint8_t>(static_cast<int8_t>(v));
}

bool Rosmaster::sendFrame(uint8_t func, const uint8_t* payload, size_t payload_len, bool fixed_len_5) {
  // Two patterns in Python:
  // - fixed length 0x05 frames: [FF, DEVICE_ID, 0x05, func, p0, p1, checksum]
  // - variable length frames:   [FF, DEVICE_ID, len-1, func, ..., checksum] where cmd[2]=len(cmd)-1
  std::vector<uint8_t> cmd;
  cmd.reserve(64);
  cmd.push_back(HEAD);
  cmd.push_back(DEVICE_ID);

  if (fixed_len_5) {
    // payload_len must be 2
    cmd.push_back(0x05);
    cmd.push_back(func);
    cmd.push_back(payload_len > 0 ? payload[0] : 0);
    cmd.push_back(payload_len > 1 ? payload[1] : 0);
    uint8_t sum = COMPLEMENT;
    for (auto b : cmd) sum = static_cast<uint8_t>(sum + b);
    cmd.push_back(sum);
    return ser_.writeAll(cmd.data(), cmd.size());
  }

  // variable length
  cmd.push_back(0x00); // placeholder
  cmd.push_back(func);
  for (size_t i = 0; i < payload_len; i++) cmd.push_back(payload[i]);
  cmd[2] = static_cast<uint8_t>(cmd.size() - 1); // like Python

  uint8_t sum = COMPLEMENT;
  for (auto b : cmd) sum = static_cast<uint8_t>(sum + b);
  cmd.push_back(sum);

  return ser_.writeAll(cmd.data(), cmd.size());
}

bool Rosmaster::requestData(uint8_t function, uint8_t param) {
  // Python __request_data:
  // cmd=[FF, DEVICE_ID, 0x05, FUNC_REQUEST_DATA, function, param, checksum]
  uint8_t payload[2] = {function, param};
  const bool ok = sendFrame(FUNC_REQUEST_DATA, payload, 2, /*fixed_len_5=*/true);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  return ok;
}

bool Rosmaster::requestVersion() {
  // Python get_version triggers __request_data(FUNC_VERSION)
  return requestData(FUNC_VERSION, 0);
}

bool Rosmaster::setAutoReport(bool enable, bool forever) {
  // Python set_auto_report_state:
  // cmd=[FF, DEVICE_ID, 0x05, FUNC_AUTO_REPORT, state1, state2, checksum]
  uint8_t state1 = enable ? 1 : 0;
  uint8_t state2 = forever ? 0x5F : 0;
  uint8_t payload[2] = {state1, state2};
  const bool ok = sendFrame(FUNC_AUTO_REPORT, payload, 2, /*fixed_len_5=*/true);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  return ok;
}

bool Rosmaster::setBeep(int on_time_ms) {
  if (on_time_ms < 0) return false;
  // Python packs int16 (h) little-endian into 2 bytes
  int16_t v = static_cast<int16_t>(on_time_ms);
  uint8_t payload[2] = { static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF) };
  const bool ok = sendFrame(FUNC_BEEP, payload, 2, /*fixed_len_5=*/true);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  return ok;
}

bool Rosmaster::setMotor(int s1, int s2, int s3, int s4) {
  // Python set_motor: FUNC_MOTOR payload is 4 signed bytes (b)
  uint8_t payload[4] = {
    limitMotorValue(s1),
    limitMotorValue(s2),
    limitMotorValue(s3),
    limitMotorValue(s4),
  };
  const bool ok = sendFrame(FUNC_MOTOR, payload, 4, /*fixed_len_5=*/false);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  return ok;
}

void Rosmaster::rxLoop() {
  // Python receive loop logic:
  // head1=read1; if 0xFF then head2=read1; if head2==DEVICE_ID-1:
  // ext_len=read1; ext_type=read1; read (ext_len-2) bytes into ext_data; last byte is rx_check_num;
  // checksum = ext_len + ext_type + sum(ext_data except last); if checksum%256 == rx_check_num => parse
  while (running_.load()) {
    uint8_t head1{};
    if (!ser_.readExact(&head1, 1)) continue;
    if (head1 != HEAD) continue;

    uint8_t head2{};
    if (!ser_.readExact(&head2, 1)) continue;
    if (head2 != static_cast<uint8_t>(DEVICE_ID - 1)) continue;

    uint8_t ext_len{};
    uint8_t ext_type{};
    if (!ser_.readExact(&ext_len, 1)) continue;
    if (!ser_.readExact(&ext_type, 1)) continue;

    const int data_len = static_cast<int>(ext_len) - 2;
    if (data_len <= 0 || data_len > 128) continue;

    std::vector<uint8_t> buf(static_cast<size_t>(data_len));
    if (!ser_.readExact(buf.data(), buf.size())) continue;

    // checksum: ext_len + ext_type + sum(all bytes except last) must match last byte
    uint8_t rx_check = buf.back();
    uint32_t sum = static_cast<uint32_t>(ext_len) + static_cast<uint32_t>(ext_type);
    for (size_t i = 0; i + 1 < buf.size(); i++) sum += buf[i];
    if (static_cast<uint8_t>(sum & 0xFF) != rx_check) {
      if (debug_) std::cout << "checksum error ext_type=" << int(ext_type) << "\n";
      continue;
    }

    parsePayload(ext_type, buf.data(), buf.size() - 1); // exclude checksum byte
  }
}

void Rosmaster::parsePayload(uint8_t ext_type, const uint8_t* d, size_t n) {
  // Mirrors __parse_data in Python (subset)
  std::scoped_lock lk(mtx_);

  if (ext_type == FUNC_REPORT_SPEED && n >= 7) {
    state_.spd.vx = static_cast<float>(le_i16(d + 0)) / 1000.0f;
    state_.spd.vy = static_cast<float>(le_i16(d + 2)) / 1000.0f;
    state_.spd.vz = static_cast<float>(le_i16(d + 4)) / 1000.0f;
    state_.spd.battery_voltage = static_cast<float>(d[6]) / 10.0f; // Python returns /10.0 in getter
    return;
  }

  if (ext_type == FUNC_REPORT_MPU_RAW && n >= 18) {
    const float gyro_ratio  = 1.0f / 3754.9f;   // same as Python
    const float accel_ratio = 1.0f / 1671.84f;  // same as Python
    const float mag_ratio   = 1.0f;

    state_.imu.gx = le_i16(d + 0) * gyro_ratio;
    state_.imu.gy = le_i16(d + 2) * -gyro_ratio;
    state_.imu.gz = le_i16(d + 4) * -gyro_ratio;

    state_.imu.ax = le_i16(d + 6)  * accel_ratio;
    state_.imu.ay = le_i16(d + 8)  * accel_ratio;
    state_.imu.az = le_i16(d + 10) * accel_ratio;

    state_.imu.mx = le_i16(d + 12) * mag_ratio;
    state_.imu.my = le_i16(d + 14) * mag_ratio;
    state_.imu.mz = le_i16(d + 16) * mag_ratio;
    return;
  }

  if (ext_type == FUNC_REPORT_ICM_RAW && n >= 18) {
    const float gyro_ratio  = 1.0f / 1000.0f;
    const float accel_ratio = 1.0f / 1000.0f;
    const float mag_ratio   = 1.0f / 1000.0f;

    state_.imu.gx = le_i16(d + 0) * gyro_ratio;
    state_.imu.gy = le_i16(d + 2) * gyro_ratio;
    state_.imu.gz = le_i16(d + 4) * gyro_ratio;

    state_.imu.ax = le_i16(d + 6)  * accel_ratio;
    state_.imu.ay = le_i16(d + 8)  * accel_ratio;
    state_.imu.az = le_i16(d + 10) * accel_ratio;

    state_.imu.mx = le_i16(d + 12) * mag_ratio;
    state_.imu.my = le_i16(d + 14) * mag_ratio;
    state_.imu.mz = le_i16(d + 16) * mag_ratio;
    return;
  }

  if (ext_type == FUNC_REPORT_IMU_ATT && n >= 6) {
    // Python: /10000.0 gives radians (?) stored; later converts to deg by *57.295...
    state_.ang.roll  = le_i16(d + 0) / 10000.0f;
    state_.ang.pitch = le_i16(d + 2) / 10000.0f;
    state_.ang.yaw   = le_i16(d + 4) / 10000.0f;
    return;
  }

  if (ext_type == FUNC_REPORT_ENCODER && n >= 16) {
    state_.enc.e1 = le_i32(d + 0);
    state_.enc.e2 = le_i32(d + 4);
    state_.enc.e3 = le_i32(d + 8);
    state_.enc.e4 = le_i32(d + 12);
    return;
  }

  if (ext_type == FUNC_VERSION && n >= 2) {
    // Python: version = H + L/10.0
    const uint8_t H = d[0];
    const uint8_t L = d[1];
    state_.version = static_cast<float>(H) + static_cast<float>(L) / 10.0f;
    return;
  }

  // (Extend here as needed)
}

} // namespace rosmaster
