#include "rosmaster/basic.hpp"
#include "rosmaster/protocol.hpp"

namespace rosmaster {

Vec3d parse_vec3d(const uint8_t* d){
  Vec3d vec;
  vec.x = le_i16(d+0);
  vec.y = le_i16(d+2);
  vec.z = le_i16(d+4);
  return vec;
}

Vec3d scale_vec3d(const Vec3d& in, float scale) {
  Vec3d out;
  out.x = in.x * scale;
  out.y = in.y * scale;
  out.z = in.z * scale;
  return out;
}

Vec3d rearrange_gyro(const Vec3d& in) {
  Vec3d out;
  out.x = +in.x;
  out.y = -in.y;
  out.z = -in.z;
  return out;
}
}