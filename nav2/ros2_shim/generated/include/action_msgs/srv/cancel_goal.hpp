#ifndef ACTION_MSGS_SRV_CANCELGOAL_HPP_
#define ACTION_MSGS_SRV_CANCELGOAL_HPP_

#include "action_msgs/msg/goal_info.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace action_msgs
{
namespace srv
{

struct CancelGoal
{
  static constexpr const char * ros2_service_type = "action_msgs/srv/CancelGoal";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "action_msgs/srv/CancelGoal_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _goal_info_type = ::action_msgs::msg::GoalInfo;
      ::action_msgs::msg::GoalInfo goal_info;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(goal_info);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(goal_info);
      }

      bool operator==(const Request & other) const
      {
        return goal_info == other.goal_info;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "action_msgs/srv/CancelGoal_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      static constexpr int8_t ERROR_NONE = 0;
      static constexpr int8_t ERROR_REJECTED = 1;
      static constexpr int8_t ERROR_UNKNOWN_GOAL_ID = 2;
      static constexpr int8_t ERROR_GOAL_TERMINATED = 3;

      using _return_code_type = int8_t;
      int8_t return_code = 0;
      using _goals_canceling_type = std::vector<::action_msgs::msg::GoalInfo>;
      std::vector<::action_msgs::msg::GoalInfo> goals_canceling;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(return_code);
        ar(goals_canceling);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(return_code);
        ar(goals_canceling);
      }

      bool operator==(const Response & other) const
      {
        return return_code == other.return_code &&
          goals_canceling == other.goals_canceling;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace action_msgs

#endif  // ACTION_MSGS_SRV_CANCELGOAL_HPP_
