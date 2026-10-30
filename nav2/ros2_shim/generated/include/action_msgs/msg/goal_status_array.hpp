#ifndef ACTION_MSGS_MSG_GOALSTATUSARRAY_HPP_
#define ACTION_MSGS_MSG_GOALSTATUSARRAY_HPP_

#include "action_msgs/msg/goal_status.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace action_msgs
{
namespace msg
{

struct GoalStatusArray
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "action_msgs/msg/GoalStatusArray";
  using SharedPtr = std::shared_ptr<GoalStatusArray>;
  using ConstSharedPtr = std::shared_ptr<const GoalStatusArray>;
  using UniquePtr = std::unique_ptr<GoalStatusArray>;
  using _status_list_type = std::vector<::action_msgs::msg::GoalStatus>;
  std::vector<::action_msgs::msg::GoalStatus> status_list;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(status_list);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(status_list);
  }

  bool operator==(const GoalStatusArray & other) const
  {
    return status_list == other.status_list;
  }
  bool operator!=(const GoalStatusArray & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace action_msgs

#endif  // ACTION_MSGS_MSG_GOALSTATUSARRAY_HPP_
