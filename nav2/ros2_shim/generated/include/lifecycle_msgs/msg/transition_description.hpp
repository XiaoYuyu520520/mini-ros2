#ifndef LIFECYCLE_MSGS_MSG_TRANSITIONDESCRIPTION_HPP_
#define LIFECYCLE_MSGS_MSG_TRANSITIONDESCRIPTION_HPP_

#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace lifecycle_msgs
{
namespace msg
{

struct TransitionDescription
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "lifecycle_msgs/msg/TransitionDescription";
  using SharedPtr = std::shared_ptr<TransitionDescription>;
  using ConstSharedPtr = std::shared_ptr<const TransitionDescription>;
  using UniquePtr = std::unique_ptr<TransitionDescription>;
  using _transition_type = ::lifecycle_msgs::msg::Transition;
  ::lifecycle_msgs::msg::Transition transition;
  using _start_state_type = ::lifecycle_msgs::msg::State;
  ::lifecycle_msgs::msg::State start_state;
  using _goal_state_type = ::lifecycle_msgs::msg::State;
  ::lifecycle_msgs::msg::State goal_state;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(transition);
    ar(start_state);
    ar(goal_state);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(transition);
    ar(start_state);
    ar(goal_state);
  }

  bool operator==(const TransitionDescription & other) const
  {
    return transition == other.transition &&
      start_state == other.start_state &&
      goal_state == other.goal_state;
  }
  bool operator!=(const TransitionDescription & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace lifecycle_msgs

#endif  // LIFECYCLE_MSGS_MSG_TRANSITIONDESCRIPTION_HPP_
