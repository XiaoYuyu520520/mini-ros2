#ifndef NAV2_MSGS_ACTION_NAVIGATETHROUGHPOSES_HPP_
#define NAV2_MSGS_ACTION_NAVIGATETHROUGHPOSES_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/msg/waypoint_status.hpp"
#include "nav_msgs/msg/goals.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct NavigateThroughPoses
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/NavigateThroughPoses_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _poses_type = ::nav_msgs::msg::Goals;
      ::nav_msgs::msg::Goals poses;
      using _behavior_tree_type = std::string;
      std::string behavior_tree;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(poses);
        ar(behavior_tree);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(poses);
        ar(behavior_tree);
      }

      bool operator==(const Goal & other) const
      {
        return poses == other.poses &&
          behavior_tree == other.behavior_tree;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/NavigateThroughPoses_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 9100;
      static constexpr uint16_t FAILED_TO_LOAD_BEHAVIOR_TREE = 9101;
      static constexpr uint16_t TF_ERROR = 9102;
      static constexpr uint16_t TIMEOUT = 9103;

      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;
      using _waypoint_statuses_type = std::vector<::nav2_msgs::msg::WaypointStatus>;
      std::vector<::nav2_msgs::msg::WaypointStatus> waypoint_statuses;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(error_code);
        ar(error_msg);
        ar(waypoint_statuses);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(error_code);
        ar(error_msg);
        ar(waypoint_statuses);
      }

      bool operator==(const Result & other) const
      {
        return error_code == other.error_code &&
          error_msg == other.error_msg &&
          waypoint_statuses == other.waypoint_statuses;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/NavigateThroughPoses_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      using _current_pose_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped current_pose;
      using _navigation_time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration navigation_time;
      using _estimated_time_remaining_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration estimated_time_remaining;
      using _number_of_recoveries_type = int16_t;
      int16_t number_of_recoveries = 0;
      using _distance_remaining_type = float;
      float distance_remaining = 0.0f;
      using _position_tracking_error_type = float;
      float position_tracking_error = 0.0f;
      using _heading_tracking_error_type = float;
      float heading_tracking_error = 0.0f;
      using _number_of_poses_remaining_type = int16_t;
      int16_t number_of_poses_remaining = 0;
      using _waypoint_statuses_type = std::vector<::nav2_msgs::msg::WaypointStatus>;
      std::vector<::nav2_msgs::msg::WaypointStatus> waypoint_statuses;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(current_pose);
        ar(navigation_time);
        ar(estimated_time_remaining);
        ar(number_of_recoveries);
        ar(distance_remaining);
        ar(position_tracking_error);
        ar(heading_tracking_error);
        ar(number_of_poses_remaining);
        ar(waypoint_statuses);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(current_pose);
        ar(navigation_time);
        ar(estimated_time_remaining);
        ar(number_of_recoveries);
        ar(distance_remaining);
        ar(position_tracking_error);
        ar(heading_tracking_error);
        ar(number_of_poses_remaining);
        ar(waypoint_statuses);
      }

      bool operator==(const Feedback & other) const
      {
        return current_pose == other.current_pose &&
          navigation_time == other.navigation_time &&
          estimated_time_remaining == other.estimated_time_remaining &&
          number_of_recoveries == other.number_of_recoveries &&
          distance_remaining == other.distance_remaining &&
          position_tracking_error == other.position_tracking_error &&
          heading_tracking_error == other.heading_tracking_error &&
          number_of_poses_remaining == other.number_of_poses_remaining &&
          waypoint_statuses == other.waypoint_statuses;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_NAVIGATETHROUGHPOSES_HPP_
