#pragma once

#include <cstdint>

namespace rosmaster {

struct Vec3d {
  float x{0.0}, y{0.0}, z{0.0};
};

struct ImuRaw {
  Vec3d acc;
  Vec3d gyro;
  Vec3d mag;
};

struct Attitude {
  float roll{0.0}, pitch{0.0}, yaw{0.0};
};

struct Encoder4 {
  int32_t m1{0}, m2{0}, m3{0}, m4{0};
};

struct Version {
  uint8_t high {0};
  uint8_t low{0};
  float version {0.0f};
};

struct State {
  ImuRaw imu;
  Attitude att;
  Encoder4 enc;
  float battery_voltage{0.0f};
};

Vec3d parse_vec3d(const uint8_t* d);
Vec3d scale_vec3d(Vec3d in, float scale);
Vec3d rearrange_gyro(Vec3d in);

} // namespace rosmaster
