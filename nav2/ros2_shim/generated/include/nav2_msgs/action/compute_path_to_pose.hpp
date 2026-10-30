#ifndef NAV2_MSGS_ACTION_COMPUTEPATHTOPOSE_HPP_
#define NAV2_MSGS_ACTION_COMPUTEPATHTOPOSE_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav_msgs/msg/path.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct ComputePathToPose
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputePathToPose_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _goal_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped goal;
      using _start_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped start;
      using _viapoints_type = std::vector<::geometry_msgs::msg::PoseStamped>;
      std::vector<::geometry_msgs::msg::PoseStamped> viapoints;
      using _planner_id_type = std::string;
      std::string planner_id;
      using _use_start_type = bool;
      bool use_start = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(goal);
        ar(start);
        ar(viapoints);
        ar(planner_id);
        ar(use_start);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(goal);
        ar(start);
        ar(viapoints);
        ar(planner_id);
        ar(use_start);
      }

      bool operator==(const Goal & other) const
      {
        return goal == other.goal &&
          start == other.start &&
          viapoints == other.viapoints &&
          planner_id == other.planner_id &&
          use_start == other.use_start;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputePathToPose_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 200;
      static constexpr uint16_t INVALID_PLANNER = 201;
      static constexpr uint16_t TF_ERROR = 202;
      static constexpr uint16_t START_OUTSIDE_MAP = 203;
      static constexpr uint16_t GOAL_OUTSIDE_MAP = 204;
      static constexpr uint16_t START_OCCUPIED = 205;
      static constexpr uint16_t GOAL_OCCUPIED = 206;
      static constexpr uint16_t TIMEOUT = 207;
      static constexpr uint16_t NO_VALID_PATH = 208;

      using _path_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path path;
      using _planning_time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration planning_time;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(path);
        ar(planning_time);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(path);
        ar(planning_time);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return path == other.path &&
          planning_time == other.planning_time &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputePathToPose_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;

      template <class Archive>
      void visit(Archive & ar)
      {
        (void)ar;
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        (void)ar;
      }

      bool operator==(const Feedback & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_COMPUTEPATHTOPOSE_HPP_
