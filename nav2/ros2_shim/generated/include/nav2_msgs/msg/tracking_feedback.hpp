#ifndef NAV2_MSGS_MSG_TRACKINGFEEDBACK_HPP_
#define NAV2_MSGS_MSG_TRACKINGFEEDBACK_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct TrackingFeedback
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/TrackingFeedback";
  using SharedPtr = std::shared_ptr<TrackingFeedback>;
  using ConstSharedPtr = std::shared_ptr<const TrackingFeedback>;
  using UniquePtr = std::unique_ptr<TrackingFeedback>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _position_tracking_error_type = float;
  float position_tracking_error = 0.0f;
  using _heading_tracking_error_type = float;
  float heading_tracking_error = 0.0f;
  using _current_path_index_type = uint32_t;
  uint32_t current_path_index = 0;
  using _robot_pose_type = ::geometry_msgs::msg::PoseStamped;
  ::geometry_msgs::msg::PoseStamped robot_pose;
  using _distance_to_goal_type = float;
  float distance_to_goal = 0.0f;
  using _speed_type = float;
  float speed = 0.0f;
  using _remaining_path_length_type = float;
  float remaining_path_length = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(position_tracking_error);
    ar(heading_tracking_error);
    ar(current_path_index);
    ar(robot_pose);
    ar(distance_to_goal);
    ar(speed);
    ar(remaining_path_length);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(position_tracking_error);
    ar(heading_tracking_error);
    ar(current_path_index);
    ar(robot_pose);
    ar(distance_to_goal);
    ar(speed);
    ar(remaining_path_length);
  }

  bool operator==(const TrackingFeedback & other) const
  {
    return header == other.header &&
      position_tracking_error == other.position_tracking_error &&
      heading_tracking_error == other.heading_tracking_error &&
      current_path_index == other.current_path_index &&
      robot_pose == other.robot_pose &&
      distance_to_goal == other.distance_to_goal &&
      speed == other.speed &&
      remaining_path_length == other.remaining_path_length;
  }
  bool operator!=(const TrackingFeedback & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_TRACKINGFEEDBACK_HPP_
