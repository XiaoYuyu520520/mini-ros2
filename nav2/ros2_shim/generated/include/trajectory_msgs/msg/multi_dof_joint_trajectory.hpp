#ifndef TRAJECTORY_MSGS_MSG_MULTIDOFJOINTTRAJECTORY_HPP_
#define TRAJECTORY_MSGS_MSG_MULTIDOFJOINTTRAJECTORY_HPP_

#include "std_msgs/msg/header.hpp"
#include "trajectory_msgs/msg/multi_dof_joint_trajectory_point.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace trajectory_msgs
{
namespace msg
{

struct MultiDOFJointTrajectory
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "trajectory_msgs/msg/MultiDOFJointTrajectory";
  using SharedPtr = std::shared_ptr<MultiDOFJointTrajectory>;
  using ConstSharedPtr = std::shared_ptr<const MultiDOFJointTrajectory>;
  using UniquePtr = std::unique_ptr<MultiDOFJointTrajectory>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _joint_names_type = std::vector<std::string>;
  std::vector<std::string> joint_names;
  using _points_type = std::vector<::trajectory_msgs::msg::MultiDOFJointTrajectoryPoint>;
  std::vector<::trajectory_msgs::msg::MultiDOFJointTrajectoryPoint> points;

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

  bool operator==(const MultiDOFJointTrajectory & other) const
  {
    return header == other.header &&
      joint_names == other.joint_names &&
      points == other.points;
  }
  bool operator!=(const MultiDOFJointTrajectory & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace trajectory_msgs

#endif  // TRAJECTORY_MSGS_MSG_MULTIDOFJOINTTRAJECTORY_HPP_
