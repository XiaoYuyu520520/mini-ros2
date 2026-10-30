#ifndef NAV2_MSGS_MSG_SPEEDLIMIT_HPP_
#define NAV2_MSGS_MSG_SPEEDLIMIT_HPP_

#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct SpeedLimit
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/SpeedLimit";
  using SharedPtr = std::shared_ptr<SpeedLimit>;
  using ConstSharedPtr = std::shared_ptr<const SpeedLimit>;
  using UniquePtr = std::unique_ptr<SpeedLimit>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _percentage_type = bool;
  bool percentage = false;
  using _speed_limit_type = double;
  double speed_limit = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(percentage);
    ar(speed_limit);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(percentage);
    ar(speed_limit);
  }

  bool operator==(const SpeedLimit & other) const
  {
    return header == other.header &&
      percentage == other.percentage &&
      speed_limit == other.speed_limit;
  }
  bool operator!=(const SpeedLimit & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_SPEEDLIMIT_HPP_
