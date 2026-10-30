#ifndef NAV2_MSGS_ACTION_FOLLOWWAYPOINTS_HPP_
#define NAV2_MSGS_ACTION_FOLLOWWAYPOINTS_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/msg/waypoint_status.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct FollowWaypoints
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowWaypoints_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _number_of_loops_type = uint32_t;
      uint32_t number_of_loops = 0;
      using _goal_index_type = uint32_t;
      uint32_t goal_index = 0;
      using _poses_type = std::vector<::geometry_msgs::msg::PoseStamped>;
      std::vector<::geometry_msgs::msg::PoseStamped> poses;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(number_of_loops);
        ar(goal_index);
        ar(poses);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(number_of_loops);
        ar(goal_index);
        ar(poses);
      }

      bool operator==(const Goal & other) const
      {
        return number_of_loops == other.number_of_loops &&
          goal_index == other.goal_index &&
          poses == other.poses;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowWaypoints_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t UNKNOWN = 600;
      static constexpr uint16_t TASK_EXECUTOR_FAILED = 601;
      static constexpr uint16_t NO_VALID_WAYPOINTS = 602;
      static constexpr uint16_t STOP_ON_MISSED_WAYPOINT = 603;

      using _missed_waypoints_type = std::vector<::nav2_msgs::msg::WaypointStatus>;
      std::vector<::nav2_msgs::msg::WaypointStatus> missed_waypoints;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(missed_waypoints);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(missed_waypoints);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return missed_waypoints == other.missed_waypoints &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/FollowWaypoints_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      using _current_waypoint_type = uint32_t;
      uint32_t current_waypoint = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(current_waypoint);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(current_waypoint);
      }

      bool operator==(const Feedback & other) const
      {
        return current_waypoint == other.current_waypoint;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_FOLLOWWAYPOINTS_HPP_
