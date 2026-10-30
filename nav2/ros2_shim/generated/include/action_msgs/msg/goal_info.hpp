#ifndef ACTION_MSGS_MSG_GOALINFO_HPP_
#define ACTION_MSGS_MSG_GOALINFO_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace action_msgs
{
namespace msg
{

struct GoalInfo
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "action_msgs/msg/GoalInfo";
  using SharedPtr = std::shared_ptr<GoalInfo>;
  using ConstSharedPtr = std::shared_ptr<const GoalInfo>;
  using UniquePtr = std::unique_ptr<GoalInfo>;
  using _goal_id_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID goal_id;
  using _stamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time stamp;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(goal_id);
    ar(stamp);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(goal_id);
    ar(stamp);
  }

  bool operator==(const GoalInfo & other) const
  {
    return goal_id == other.goal_id &&
      stamp == other.stamp;
  }
  bool operator!=(const GoalInfo & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace action_msgs

#endif  // ACTION_MSGS_MSG_GOALINFO_HPP_
