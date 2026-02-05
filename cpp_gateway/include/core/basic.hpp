#pragma once

#include <cstdint>

namespace core {

struct Vec3d {
  float x{0.0}, y{0.0}, z{0.0};
};

// IMU combined data
struct IMUData {
    Vec3d acc;
    Vec3d gyro;
    Vec3d mag;
};

// Euler angles
struct Angles {
    float roll{0.0};
    float pitch{0.0};
    float yaw{0.0};
};

// Encoder data
struct Encoders {
    int32_t e1{0};
    int32_t e2{0};
    int32_t e3{0};
    int32_t e4{0};
};

// State structure (sensor readings from robot)
struct State {
    IMUData imu;
    Angles ang;
    Encoders enc;
    float battery_voltage{0.0f};
};

// Motor commands structure
struct MotorCommands {
    int16_t m1{0};
    int16_t m2{0};
    int16_t m3{0};
    int16_t m4{0};
};

// Actions structure (commands sent to robot)
struct Actions {
    MotorCommands motors;
    uint16_t beep_ms{0};
    uint8_t flags{0};
};

// Timestamp pair (wall clock and monotonic)
struct Timestamps {
    double epoch_s{0.0};
    double mono_s{0.0};
};

struct Version {
  uint8_t high {0};
  uint8_t low{0};
  float version {0.0f};
};

Vec3d parse_vec3d(const uint8_t* d);
Vec3d scale_vec3d(Vec3d in, float scale);
Vec3d rearrange_gyro(Vec3d in);

} // namespace core
