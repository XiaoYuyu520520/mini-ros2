#ifndef TRAJECTORY_MSGS_MSG_MULTIDOFJOINTTRAJECTORYPOINT_HPP_
#define TRAJECTORY_MSGS_MSG_MULTIDOFJOINTTRAJECTORYPOINT_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/transform.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace trajectory_msgs
{
namespace msg
{

struct MultiDOFJointTrajectoryPoint
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "trajectory_msgs/msg/MultiDOFJointTrajectoryPoint";
  using SharedPtr = std::shared_ptr<MultiDOFJointTrajectoryPoint>;
  using ConstSharedPtr = std::shared_ptr<const MultiDOFJointTrajectoryPoint>;
  using UniquePtr = std::unique_ptr<MultiDOFJointTrajectoryPoint>;
  using _transforms_type = std::vector<::geometry_msgs::msg::Transform>;
  std::vector<::geometry_msgs::msg::Transform> transforms;
  using _velocities_type = std::vector<::geometry_msgs::msg::Twist>;
  std::vector<::geometry_msgs::msg::Twist> velocities;
  using _accelerations_type = std::vector<::geometry_msgs::msg::Twist>;
  std::vector<::geometry_msgs::msg::Twist> accelerations;
  using _time_from_start_type = ::builtin_interfaces::msg::Duration;
  ::builtin_interfaces::msg::Duration time_from_start;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(transforms);
    ar(velocities);
    ar(accelerations);
    ar(time_from_start);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(transforms);
    ar(velocities);
    ar(accelerations);
    ar(time_from_start);
  }

  bool operator==(const MultiDOFJointTrajectoryPoint & other) const
  {
    return transforms == other.transforms &&
      velocities == other.velocities &&
      accelerations == other.accelerations &&
      time_from_start == other.time_from_start;
  }
  bool operator!=(const MultiDOFJointTrajectoryPoint & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace trajectory_msgs

#endif  // TRAJECTORY_MSGS_MSG_MULTIDOFJOINTTRAJECTORYPOINT_HPP_
