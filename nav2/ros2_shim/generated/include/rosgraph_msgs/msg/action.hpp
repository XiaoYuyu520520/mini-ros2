#ifndef ROSGRAPH_MSGS_MSG_ACTION_HPP_
#define ROSGRAPH_MSGS_MSG_ACTION_HPP_

#include "rosgraph_msgs/msg/service.hpp"
#include "rosgraph_msgs/msg/topic.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct Action
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/Action";
  using SharedPtr = std::shared_ptr<Action>;
  using ConstSharedPtr = std::shared_ptr<const Action>;
  using UniquePtr = std::unique_ptr<Action>;
  using _name_type = std::string;
  std::string name;
  using _send_goal_type = ::rosgraph_msgs::msg::Service;
  ::rosgraph_msgs::msg::Service send_goal;
  using _get_result_type = ::rosgraph_msgs::msg::Service;
  ::rosgraph_msgs::msg::Service get_result;
  using _cancel_goal_type = ::rosgraph_msgs::msg::Service;
  ::rosgraph_msgs::msg::Service cancel_goal;
  using _feedback_type = ::rosgraph_msgs::msg::Topic;
  ::rosgraph_msgs::msg::Topic feedback;
  using _status_type = ::rosgraph_msgs::msg::Topic;
  ::rosgraph_msgs::msg::Topic status;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(send_goal);
    ar(get_result);
    ar(cancel_goal);
    ar(feedback);
    ar(status);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(send_goal);
    ar(get_result);
    ar(cancel_goal);
    ar(feedback);
    ar(status);
  }

  bool operator==(const Action & other) const
  {
    return name == other.name &&
      send_goal == other.send_goal &&
      get_result == other.get_result &&
      cancel_goal == other.cancel_goal &&
      feedback == other.feedback &&
      status == other.status;
  }
  bool operator!=(const Action & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_ACTION_HPP_
