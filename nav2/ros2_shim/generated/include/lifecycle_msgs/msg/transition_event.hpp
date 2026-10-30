#ifndef LIFECYCLE_MSGS_MSG_TRANSITIONEVENT_HPP_
#define LIFECYCLE_MSGS_MSG_TRANSITIONEVENT_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace msg
{

struct TransitionEvent
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "lifecycle_msgs/msg/TransitionEvent";
  using SharedPtr = std::shared_ptr<TransitionEvent>;
  using ConstSharedPtr = std::shared_ptr<const TransitionEvent>;
  using UniquePtr = std::unique_ptr<TransitionEvent>;
  using _stamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time stamp;
  using _transition_type = ::lifecycle_msgs::msg::Transition;
  ::lifecycle_msgs::msg::Transition transition;
  using _start_state_type = ::lifecycle_msgs::msg::State;
  ::lifecycle_msgs::msg::State start_state;
  using _goal_state_type = ::lifecycle_msgs::msg::State;
  ::lifecycle_msgs::msg::State goal_state;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(stamp);
    ar(transition);
    ar(start_state);
    ar(goal_state);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(stamp);
    ar(transition);
    ar(start_state);
    ar(goal_state);
  }

  bool operator==(const TransitionEvent & other) const
  {
    return stamp == other.stamp &&
      transition == other.transition &&
      start_state == other.start_state &&
      goal_state == other.goal_state;
  }
  bool operator!=(const TransitionEvent & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_MSG_TRANSITIONEVENT_HPP_
