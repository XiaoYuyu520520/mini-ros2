#ifndef NAV2_MSGS_MSG_BEHAVIORTREESTATUSCHANGE_HPP_
#define NAV2_MSGS_MSG_BEHAVIORTREESTATUSCHANGE_HPP_

#include "builtin_interfaces/msg/time.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct BehaviorTreeStatusChange
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/BehaviorTreeStatusChange";
  using SharedPtr = std::shared_ptr<BehaviorTreeStatusChange>;
  using ConstSharedPtr = std::shared_ptr<const BehaviorTreeStatusChange>;
  using UniquePtr = std::unique_ptr<BehaviorTreeStatusChange>;
  using _timestamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time timestamp;
  using _node_name_type = std::string;
  std::string node_name;
  using _uid_type = uint16_t;
  uint16_t uid = 0;
  using _previous_status_type = std::string;
  std::string previous_status;
  using _current_status_type = std::string;
  std::string current_status;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(timestamp);
    ar(node_name);
    ar(uid);
    ar(previous_status);
    ar(current_status);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(timestamp);
    ar(node_name);
    ar(uid);
    ar(previous_status);
    ar(current_status);
  }

  bool operator==(const BehaviorTreeStatusChange & other) const
  {
    return timestamp == other.timestamp &&
      node_name == other.node_name &&
      uid == other.uid &&
      previous_status == other.previous_status &&
      current_status == other.current_status;
  }
  bool operator!=(const BehaviorTreeStatusChange & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_BEHAVIORTREESTATUSCHANGE_HPP_
