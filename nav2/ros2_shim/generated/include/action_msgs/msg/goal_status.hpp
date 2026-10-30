#ifndef ACTION_MSGS_MSG_GOALSTATUS_HPP_
#define ACTION_MSGS_MSG_GOALSTATUS_HPP_

#include "action_msgs/msg/goal_info.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace action_msgs
{
namespace msg
{

struct GoalStatus
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "action_msgs/msg/GoalStatus";
  using SharedPtr = std::shared_ptr<GoalStatus>;
  using ConstSharedPtr = std::shared_ptr<const GoalStatus>;
  using UniquePtr = std::unique_ptr<GoalStatus>;
  static constexpr int8_t STATUS_UNKNOWN = 0;
  static constexpr int8_t STATUS_ACCEPTED = 1;
  static constexpr int8_t STATUS_EXECUTING = 2;
  static constexpr int8_t STATUS_CANCELING = 3;
  static constexpr int8_t STATUS_SUCCEEDED = 4;
  static constexpr int8_t STATUS_CANCELED = 5;
  static constexpr int8_t STATUS_ABORTED = 6;

  using _goal_info_type = ::action_msgs::msg::GoalInfo;
  ::action_msgs::msg::GoalInfo goal_info;
  using _status_type = int8_t;
  int8_t status = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(goal_info);
    ar(status);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(goal_info);
    ar(status);
  }

  bool operator==(const GoalStatus & other) const
  {
    return goal_info == other.goal_info &&
      status == other.status;
  }
  bool operator!=(const GoalStatus & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace action_msgs

#endif  // ACTION_MSGS_MSG_GOALSTATUS_HPP_
