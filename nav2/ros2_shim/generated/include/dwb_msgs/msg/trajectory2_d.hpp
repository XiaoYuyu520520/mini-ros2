#ifndef DWB_MSGS_MSG_TRAJECTORY2D_HPP_
#define DWB_MSGS_MSG_TRAJECTORY2D_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "nav_2d_msgs/msg/twist2_d.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace dwb_msgs
{
namespace msg
{

struct Trajectory2D
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "dwb_msgs/msg/Trajectory2D";
  using SharedPtr = std::shared_ptr<Trajectory2D>;
  using ConstSharedPtr = std::shared_ptr<const Trajectory2D>;
  using UniquePtr = std::unique_ptr<Trajectory2D>;
  using _velocity_type = ::nav_2d_msgs::msg::Twist2D;
  ::nav_2d_msgs::msg::Twist2D velocity;
  using _time_offsets_type = std::vector<::builtin_interfaces::msg::Duration>;
  std::vector<::builtin_interfaces::msg::Duration> time_offsets;
  using _poses_type = std::vector<::geometry_msgs::msg::Pose>;
  std::vector<::geometry_msgs::msg::Pose> poses;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(velocity);
    ar(time_offsets);
    ar(poses);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(velocity);
    ar(time_offsets);
    ar(poses);
  }

  bool operator==(const Trajectory2D & other) const
  {
    return velocity == other.velocity &&
      time_offsets == other.time_offsets &&
      poses == other.poses;
  }
  bool operator!=(const Trajectory2D & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace dwb_msgs

#endif  // DWB_MSGS_MSG_TRAJECTORY2D_HPP_
