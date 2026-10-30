#ifndef NAV2_MSGS_ACTION_COMPUTEROUTE_HPP_
#define NAV2_MSGS_ACTION_COMPUTEROUTE_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/msg/route.hpp"
#include "nav_msgs/msg/path.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct ComputeRoute
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputeRoute_Goal";
      using SharedPtr = std::shared_ptr<Goal>;
      using ConstSharedPtr = std::shared_ptr<const Goal>;
      using UniquePtr = std::unique_ptr<Goal>;
      using _start_id_type = uint16_t;
      uint16_t start_id = 0;
      using _start_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped start;
      using _goal_id_type = uint16_t;
      uint16_t goal_id = 0;
      using _goal_type = ::geometry_msgs::msg::PoseStamped;
      ::geometry_msgs::msg::PoseStamped goal;
      using _use_start_type = bool;
      bool use_start = false;
      using _use_poses_type = bool;
      bool use_poses = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(start_id);
        ar(start);
        ar(goal_id);
        ar(goal);
        ar(use_start);
        ar(use_poses);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(start_id);
        ar(start);
        ar(goal_id);
        ar(goal);
        ar(use_start);
        ar(use_poses);
      }

      bool operator==(const Goal & other) const
      {
        return start_id == other.start_id &&
          start == other.start &&
          goal_id == other.goal_id &&
          goal == other.goal &&
          use_start == other.use_start &&
          use_poses == other.use_poses;
      }
      bool operator!=(const Goal & other) const {return !(*this == other);}
    };

    struct Result
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputeRoute_Result";
      using SharedPtr = std::shared_ptr<Result>;
      using ConstSharedPtr = std::shared_ptr<const Result>;
      using UniquePtr = std::unique_ptr<Result>;
      static constexpr uint16_t NONE = 0;
      static constexpr uint16_t GOAL_REJECTED = 1;
      static constexpr uint16_t SEND_GOAL_FAILURE = 2;
      static constexpr uint16_t UNKNOWN = 400;
      static constexpr uint16_t TF_ERROR = 401;
      static constexpr uint16_t NO_VALID_GRAPH = 402;
      static constexpr uint16_t INDETERMINANT_NODES_ON_GRAPH = 403;
      static constexpr uint16_t TIMEOUT = 404;
      static constexpr uint16_t NO_VALID_ROUTE = 405;
      static constexpr uint16_t INVALID_EDGE_SCORER_USE = 407;

      using _planning_time_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration planning_time;
      using _path_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path path;
      using _route_type = ::nav2_msgs::msg::Route;
      ::nav2_msgs::msg::Route route;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(planning_time);
        ar(path);
        ar(route);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(planning_time);
        ar(path);
        ar(route);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return planning_time == other.planning_time &&
          path == other.path &&
          route == other.route &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputeRoute_Feedback";
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

#endif  // NAV2_MSGS_ACTION_COMPUTEROUTE_HPP_
