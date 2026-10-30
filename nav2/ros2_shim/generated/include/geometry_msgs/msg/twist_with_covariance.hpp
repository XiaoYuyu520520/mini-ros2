#ifndef GEOMETRY_MSGS_MSG_TWISTWITHCOVARIANCE_HPP_
#define GEOMETRY_MSGS_MSG_TWISTWITHCOVARIANCE_HPP_

#include <array>
#include "geometry_msgs/msg/twist.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct TwistWithCovariance
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/TwistWithCovariance";
  using SharedPtr = std::shared_ptr<TwistWithCovariance>;
  using ConstSharedPtr = std::shared_ptr<const TwistWithCovariance>;
  using UniquePtr = std::unique_ptr<TwistWithCovariance>;
  using _twist_type = ::geometry_msgs::msg::Twist;
  ::geometry_msgs::msg::Twist twist;
  using _covariance_type = std::array<double, 36>;
  std::array<double, 36> covariance = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(twist);
    ar(covariance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(twist);
    ar(covariance);
  }

  bool operator==(const TwistWithCovariance & other) const
  {
    return twist == other.twist &&
      covariance == other.covariance;
  }
  bool operator!=(const TwistWithCovariance & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_TWISTWITHCOVARIANCE_HPP_
