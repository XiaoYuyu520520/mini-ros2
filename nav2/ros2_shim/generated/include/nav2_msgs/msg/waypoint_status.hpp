#ifndef NAV2_MSGS_MSG_WAYPOINTSTATUS_HPP_
#define NAV2_MSGS_MSG_WAYPOINTSTATUS_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct WaypointStatus
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/WaypointStatus";
  using SharedPtr = std::shared_ptr<WaypointStatus>;
  using ConstSharedPtr = std::shared_ptr<const WaypointStatus>;
  using UniquePtr = std::unique_ptr<WaypointStatus>;
  static constexpr uint8_t PENDING = 0;
  static constexpr uint8_t COMPLETED = 1;
  static constexpr uint8_t SKIPPED = 2;
  static constexpr uint8_t FAILED = 3;

  using _waypoint_status_type = uint8_t;
  uint8_t waypoint_status = 0;
  using _waypoint_index_type = uint32_t;
  uint32_t waypoint_index = 0;
  using _waypoint_pose_type = ::geometry_msgs::msg::PoseStamped;
  ::geometry_msgs::msg::PoseStamped waypoint_pose;
  using _error_code_type = uint16_t;
  uint16_t error_code = 0;
  using _error_msg_type = std::string;
  std::string error_msg;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(waypoint_status);
    ar(waypoint_index);
    ar(waypoint_pose);
    ar(error_code);
    ar(error_msg);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(waypoint_status);
    ar(waypoint_index);
    ar(waypoint_pose);
    ar(error_code);
    ar(error_msg);
  }

  bool operator==(const WaypointStatus & other) const
  {
    return waypoint_status == other.waypoint_status &&
      waypoint_index == other.waypoint_index &&
      waypoint_pose == other.waypoint_pose &&
      error_code == other.error_code &&
      error_msg == other.error_msg;
  }
  bool operator!=(const WaypointStatus & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_WAYPOINTSTATUS_HPP_
