#ifndef ROSGRAPH_MSGS_MSG_QOSPROFILE_HPP_
#define ROSGRAPH_MSGS_MSG_QOSPROFILE_HPP_

#include "builtin_interfaces/msg/duration.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct QoSProfile
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/QoSProfile";
  using SharedPtr = std::shared_ptr<QoSProfile>;
  using ConstSharedPtr = std::shared_ptr<const QoSProfile>;
  using UniquePtr = std::unique_ptr<QoSProfile>;
  static constexpr uint8_t HISTORY_SYSTEM_DEFAULT = 0;
  static constexpr uint8_t HISTORY_KEEP_LAST = 1;
  static constexpr uint8_t HISTORY_KEEP_ALL = 2;
  static constexpr uint8_t HISTORY_UNKNOWN = 3;
  static constexpr uint8_t RELIABILITY_SYSTEM_DEFAULT = 0;
  static constexpr uint8_t RELIABILITY_RELIABLE = 1;
  static constexpr uint8_t RELIABILITY_BEST_EFFORT = 2;
  static constexpr uint8_t RELIABILITY_UNKNOWN = 3;
  static constexpr uint8_t RELIABILITY_BEST_AVAILABLE = 4;
  static constexpr uint8_t DURABILITY_SYSTEM_DEFAULT = 0;
  static constexpr uint8_t DURABILITY_TRANSIENT_LOCAL = 1;
  static constexpr uint8_t DURABILITY_VOLATILE = 2;
  static constexpr uint8_t DURABILITY_UNKNOWN = 3;
  static constexpr uint8_t DURABILITY_BEST_AVAILABLE = 4;
  static constexpr uint8_t LIVELINESS_SYSTEM_DEFAULT = 0;
  static constexpr uint8_t LIVELINESS_AUTOMATIC = 1;
  static constexpr uint8_t LIVELINESS_MANUAL_BY_TOPIC = 3;
  static constexpr uint8_t LIVELINESS_UNKNOWN = 4;
  static constexpr uint8_t LIVELINESS_BEST_AVAILABLE = 5;

  using _depth_type = uint32_t;
  uint32_t depth = 0;
  using _deadline_type = ::builtin_interfaces::msg::Duration;
  ::builtin_interfaces::msg::Duration deadline;
  using _lifespan_type = ::builtin_interfaces::msg::Duration;
  ::builtin_interfaces::msg::Duration lifespan;
  using _history_type = uint8_t;
  uint8_t history = 0;
  using _reliability_type = uint8_t;
  uint8_t reliability = 0;
  using _durability_type = uint8_t;
  uint8_t durability = 0;
  using _liveliness_type = uint8_t;
  uint8_t liveliness = 0;
  using _liveliness_lease_duration_type = ::builtin_interfaces::msg::Duration;
  ::builtin_interfaces::msg::Duration liveliness_lease_duration;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(depth);
    ar(deadline);
    ar(lifespan);
    ar(history);
    ar(reliability);
    ar(durability);
    ar(liveliness);
    ar(liveliness_lease_duration);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(depth);
    ar(deadline);
    ar(lifespan);
    ar(history);
    ar(reliability);
    ar(durability);
    ar(liveliness);
    ar(liveliness_lease_duration);
  }

  bool operator==(const QoSProfile & other) const
  {
    return depth == other.depth &&
      deadline == other.deadline &&
      lifespan == other.lifespan &&
      history == other.history &&
      reliability == other.reliability &&
      durability == other.durability &&
      liveliness == other.liveliness &&
      liveliness_lease_duration == other.liveliness_lease_duration;
  }
  bool operator!=(const QoSProfile & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_QOSPROFILE_HPP_
