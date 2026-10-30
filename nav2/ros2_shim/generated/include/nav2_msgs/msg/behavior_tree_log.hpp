#ifndef NAV2_MSGS_MSG_BEHAVIORTREELOG_HPP_
#define NAV2_MSGS_MSG_BEHAVIORTREELOG_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "nav2_msgs/msg/behavior_tree_status_change.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct BehaviorTreeLog
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/BehaviorTreeLog";
  using SharedPtr = std::shared_ptr<BehaviorTreeLog>;
  using ConstSharedPtr = std::shared_ptr<const BehaviorTreeLog>;
  using UniquePtr = std::unique_ptr<BehaviorTreeLog>;
  using _timestamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time timestamp;
  using _event_log_type = std::vector<::nav2_msgs::msg::BehaviorTreeStatusChange>;
  std::vector<::nav2_msgs::msg::BehaviorTreeStatusChange> event_log;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(timestamp);
    ar(event_log);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(timestamp);
    ar(event_log);
  }

  bool operator==(const BehaviorTreeLog & other) const
  {
    return timestamp == other.timestamp &&
      event_log == other.event_log;
  }
  bool operator!=(const BehaviorTreeLog & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_BEHAVIORTREELOG_HPP_
