#ifndef SENSOR_MSGS_MSG_TIMEREFERENCE_HPP_
#define SENSOR_MSGS_MSG_TIMEREFERENCE_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct TimeReference
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/TimeReference";
  using SharedPtr = std::shared_ptr<TimeReference>;
  using ConstSharedPtr = std::shared_ptr<const TimeReference>;
  using UniquePtr = std::unique_ptr<TimeReference>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _time_ref_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time time_ref;
  using _source_type = std::string;
  std::string source;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(time_ref);
    ar(source);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(time_ref);
    ar(source);
  }

  bool operator==(const TimeReference & other) const
  {
    return header == other.header &&
      time_ref == other.time_ref &&
      source == other.source;
  }
  bool operator!=(const TimeReference & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_TIMEREFERENCE_HPP_
