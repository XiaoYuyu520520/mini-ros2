#ifndef NAV_MSGS_MSG_TRAJECTORYPOINT_HPP_
#define NAV_MSGS_MSG_TRAJECTORYPOINT_HPP_

#include "geometry_msgs/msg/accel.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "geometry_msgs/msg/wrench.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct TrajectoryPoint
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/TrajectoryPoint";
  using SharedPtr = std::shared_ptr<TrajectoryPoint>;
  using ConstSharedPtr = std::shared_ptr<const TrajectoryPoint>;
  using UniquePtr = std::unique_ptr<TrajectoryPoint>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;
  using _velocity_type = ::geometry_msgs::msg::Twist;
  ::geometry_msgs::msg::Twist velocity;
  using _acceleration_type = ::geometry_msgs::msg::Accel;
  ::geometry_msgs::msg::Accel acceleration;
  using _effort_type = ::geometry_msgs::msg::Wrench;
  ::geometry_msgs::msg::Wrench effort;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(pose);
    ar(velocity);
    ar(acceleration);
    ar(effort);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(pose);
    ar(velocity);
    ar(acceleration);
    ar(effort);
  }

  bool operator==(const TrajectoryPoint & other) const
  {
    return header == other.header &&
      pose == other.pose &&
      velocity == other.velocity &&
      acceleration == other.acceleration &&
      effort == other.effort;
  }
  bool operator!=(const TrajectoryPoint & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_TRAJECTORYPOINT_HPP_
