#ifndef GEOMETRY_MSGS_MSG_TWISTSTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_TWISTSTAMPED_HPP_

#include "geometry_msgs/msg/twist.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct TwistStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/TwistStamped";
  using SharedPtr = std::shared_ptr<TwistStamped>;
  using ConstSharedPtr = std::shared_ptr<const TwistStamped>;
  using UniquePtr = std::unique_ptr<TwistStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _twist_type = ::geometry_msgs::msg::Twist;
  ::geometry_msgs::msg::Twist twist;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(twist);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(twist);
  }

  bool operator==(const TwistStamped & other) const
  {
    return header == other.header &&
      twist == other.twist;
  }
  bool operator!=(const TwistStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_TWISTSTAMPED_HPP_
