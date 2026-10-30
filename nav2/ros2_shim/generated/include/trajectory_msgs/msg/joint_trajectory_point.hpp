#ifndef TRAJECTORY_MSGS_MSG_JOINTTRAJECTORYPOINT_HPP_
#define TRAJECTORY_MSGS_MSG_JOINTTRAJECTORYPOINT_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace trajectory_msgs
{
namespace msg
{

struct JointTrajectoryPoint
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "trajectory_msgs/msg/JointTrajectoryPoint";
  using SharedPtr = std::shared_ptr<JointTrajectoryPoint>;
  using ConstSharedPtr = std::shared_ptr<const JointTrajectoryPoint>;
  using UniquePtr = std::unique_ptr<JointTrajectoryPoint>;
  using _positions_type = std::vector<double>;
  std::vector<double> positions;
  using _velocities_type = std::vector<double>;
  std::vector<double> velocities;
  using _accelerations_type = std::vector<double>;
  std::vector<double> accelerations;
  using _effort_type = std::vector<double>;
  std::vector<double> effort;
  using _time_from_start_type = ::builtin_interfaces::msg::Duration;
  ::builtin_interfaces::msg::Duration time_from_start;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(positions);
    ar(velocities);
    ar(accelerations);
    ar(effort);
    ar(time_from_start);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(positions);
    ar(velocities);
    ar(accelerations);
    ar(effort);
    ar(time_from_start);
  }

  bool operator==(const JointTrajectoryPoint & other) const
  {
    return positions == other.positions &&
      velocities == other.velocities &&
      accelerations == other.accelerations &&
      effort == other.effort &&
      time_from_start == other.time_from_start;
  }
  bool operator!=(const JointTrajectoryPoint & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace trajectory_msgs

#endif  // TRAJECTORY_MSGS_MSG_JOINTTRAJECTORYPOINT_HPP_
