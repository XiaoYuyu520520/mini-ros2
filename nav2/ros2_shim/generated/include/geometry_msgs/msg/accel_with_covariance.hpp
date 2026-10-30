#ifndef GEOMETRY_MSGS_MSG_ACCELWITHCOVARIANCE_HPP_
#define GEOMETRY_MSGS_MSG_ACCELWITHCOVARIANCE_HPP_

#include <array>
#include "geometry_msgs/msg/accel.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct AccelWithCovariance
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/AccelWithCovariance";
  using SharedPtr = std::shared_ptr<AccelWithCovariance>;
  using ConstSharedPtr = std::shared_ptr<const AccelWithCovariance>;
  using UniquePtr = std::unique_ptr<AccelWithCovariance>;
  using _accel_type = ::geometry_msgs::msg::Accel;
  ::geometry_msgs::msg::Accel accel;
  using _covariance_type = std::array<double, 36>;
  std::array<double, 36> covariance = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(accel);
    ar(covariance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(accel);
    ar(covariance);
  }

  bool operator==(const AccelWithCovariance & other) const
  {
    return accel == other.accel &&
      covariance == other.covariance;
  }
  bool operator!=(const AccelWithCovariance & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_ACCELWITHCOVARIANCE_HPP_
