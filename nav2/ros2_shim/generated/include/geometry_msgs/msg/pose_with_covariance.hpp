#ifndef GEOMETRY_MSGS_MSG_POSEWITHCOVARIANCE_HPP_
#define GEOMETRY_MSGS_MSG_POSEWITHCOVARIANCE_HPP_

#include <array>
#include "geometry_msgs/msg/pose.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct PoseWithCovariance
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/PoseWithCovariance";
  using SharedPtr = std::shared_ptr<PoseWithCovariance>;
  using ConstSharedPtr = std::shared_ptr<const PoseWithCovariance>;
  using UniquePtr = std::unique_ptr<PoseWithCovariance>;
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;
  using _covariance_type = std::array<double, 36>;
  std::array<double, 36> covariance = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(pose);
    ar(covariance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(pose);
    ar(covariance);
  }

  bool operator==(const PoseWithCovariance & other) const
  {
    return pose == other.pose &&
      covariance == other.covariance;
  }
  bool operator!=(const PoseWithCovariance & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POSEWITHCOVARIANCE_HPP_
