#ifndef NAV2_MSGS_ACTION_COMPUTEANDTRACKROUTE_HPP_
#define NAV2_MSGS_ACTION_COMPUTEANDTRACKROUTE_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/msg/route.hpp"
#include "nav_msgs/msg/path.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace action
{

struct ComputeAndTrackRoute
{
    struct Goal
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputeAndTrackRoute_Goal";
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
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputeAndTrackRoute_Result";
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
      static constexpr uint16_t OPERATION_FAILED = 406;
      static constexpr uint16_t INVALID_EDGE_SCORER_USE = 407;

      using _execution_duration_type = ::builtin_interfaces::msg::Duration;
      ::builtin_interfaces::msg::Duration execution_duration;
      using _error_code_type = uint16_t;
      uint16_t error_code = 0;
      using _error_msg_type = std::string;
      std::string error_msg;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(execution_duration);
        ar(error_code);
        ar(error_msg);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(execution_duration);
        ar(error_code);
        ar(error_msg);
      }

      bool operator==(const Result & other) const
      {
        return execution_duration == other.execution_duration &&
          error_code == other.error_code &&
          error_msg == other.error_msg;
      }
      bool operator!=(const Result & other) const {return !(*this == other);}
    };

    struct Feedback
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "nav2_msgs/action/ComputeAndTrackRoute_Feedback";
      using SharedPtr = std::shared_ptr<Feedback>;
      using ConstSharedPtr = std::shared_ptr<const Feedback>;
      using UniquePtr = std::unique_ptr<Feedback>;
      using _last_node_id_type = uint16_t;
      uint16_t last_node_id = 0;
      using _next_node_id_type = uint16_t;
      uint16_t next_node_id = 0;
      using _current_edge_id_type = uint16_t;
      uint16_t current_edge_id = 0;
      using _route_type = ::nav2_msgs::msg::Route;
      ::nav2_msgs::msg::Route route;
      using _path_type = ::nav_msgs::msg::Path;
      ::nav_msgs::msg::Path path;
      using _operations_triggered_type = std::vector<std::string>;
      std::vector<std::string> operations_triggered;
      using _rerouted_type = bool;
      bool rerouted = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(last_node_id);
        ar(next_node_id);
        ar(current_edge_id);
        ar(route);
        ar(path);
        ar(operations_triggered);
        ar(rerouted);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(last_node_id);
        ar(next_node_id);
        ar(current_edge_id);
        ar(route);
        ar(path);
        ar(operations_triggered);
        ar(rerouted);
      }

      bool operator==(const Feedback & other) const
      {
        return last_node_id == other.last_node_id &&
          next_node_id == other.next_node_id &&
          current_edge_id == other.current_edge_id &&
          route == other.route &&
          path == other.path &&
          operations_triggered == other.operations_triggered &&
          rerouted == other.rerouted;
      }
      bool operator!=(const Feedback & other) const {return !(*this == other);}
    };
};

}  // namespace action
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_ACTION_COMPUTEANDTRACKROUTE_HPP_
