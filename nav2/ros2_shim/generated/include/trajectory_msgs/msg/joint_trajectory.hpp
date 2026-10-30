#ifndef TRAJECTORY_MSGS_MSG_JOINTTRAJECTORY_HPP_
#define TRAJECTORY_MSGS_MSG_JOINTTRAJECTORY_HPP_

#include "std_msgs/msg/header.hpp"
#include "trajectory_msgs/msg/joint_trajectory_point.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace trajectory_msgs
{
namespace msg
{

struct JointTrajectory
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "trajectory_msgs/msg/JointTrajectory";
  using SharedPtr = std::shared_ptr<JointTrajectory>;
  using ConstSharedPtr = std::shared_ptr<const JointTrajectory>;
  using UniquePtr = std::unique_ptr<JointTrajectory>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _joint_names_type = std::vector<std::string>;
  std::vector<std::string> joint_names;
  using _points_type = std::vector<::trajectory_msgs::msg::JointTrajectoryPoint>;
  std::vector<::trajectory_msgs::msg::JointTrajectoryPoint> points;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(joint_names);
    ar(points);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(joint_names);
    ar(points);
  }

  bool operator==(const JointTrajectory & other) const
  {
    return header == other.header &&
      joint_names == other.joint_names &&
      points == other.points;
  }
  bool operator!=(const JointTrajectory & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace trajectory_msgs

#endif  // TRAJECTORY_MSGS_MSG_JOINTTRAJECTORY_HPP_
