#ifndef TF2__UTILS_HPP_
#define TF2__UTILS_HPP_

#include <cmath>
#include <string>

#include "geometry_msgs/msg/quaternion.hpp"
#include "tf2/LinearMath/Quaternion.hpp"

namespace tf2
{

inline double getYaw(const geometry_msgs::msg::Quaternion & q)
{
  return std::atan2(
    2.0 * (q.w * q.z + q.x * q.y),
    1.0 - 2.0 * (q.y * q.y + q.z * q.z));
}

template<class T>
inline void getEulerYPR(const T &, double & yaw, double & pitch, double & roll)
{
  yaw = 0;
  pitch = 0;
  roll = 0;
}

}  // namespace tf2

#endif  // TF2__UTILS_HPP_
