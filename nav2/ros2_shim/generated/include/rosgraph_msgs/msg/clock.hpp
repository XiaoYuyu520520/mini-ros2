#ifndef ROSGRAPH_MSGS_MSG_CLOCK_HPP_
#define ROSGRAPH_MSGS_MSG_CLOCK_HPP_

#include "builtin_interfaces/msg/time.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct Clock
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/Clock";
  using SharedPtr = std::shared_ptr<Clock>;
  using ConstSharedPtr = std::shared_ptr<const Clock>;
  using UniquePtr = std::unique_ptr<Clock>;
  using _clock_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time clock;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(clock);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(clock);
  }

  bool operator==(const Clock & other) const
  {
    return clock == other.clock;
  }
  bool operator!=(const Clock & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_CLOCK_HPP_
