#include "core/basic.hpp"
#include "utils/cast.hpp"
#include "rosmaster/protocol.hpp"

namespace core {

Vec3d parse_vec3d(const uint8_t* d){
  Vec3d vec;
  vec.x = utils::le_i16(d+0);
  vec.y = utils::le_i16(d+2);
  vec.z = utils::le_i16(d+4);
  return vec;
}

Vec3d scale_vec3d(Vec3d in, float scale) {
  Vec3d out;
  out.x = in.x * scale;
  out.y = in.y * scale;
  out.z = in.z * scale;
  return out;
}

Vec3d rearrange_gyro(Vec3d in) {
  Vec3d out;
  out.x = +in.x;
  out.y = -in.y;
  out.z = -in.z;
  return out;
}
}