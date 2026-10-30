#ifndef NAV_2D_MSGS_MSG_TWIST2DSTAMPED_HPP_
#define NAV_2D_MSGS_MSG_TWIST2DSTAMPED_HPP_

#include "nav_2d_msgs/msg/twist2_d.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav_2d_msgs
{
namespace msg
{

struct Twist2DStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_2d_msgs/msg/Twist2DStamped";
  using SharedPtr = std::shared_ptr<Twist2DStamped>;
  using ConstSharedPtr = std::shared_ptr<const Twist2DStamped>;
  using UniquePtr = std::unique_ptr<Twist2DStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _velocity_type = ::nav_2d_msgs::msg::Twist2D;
  ::nav_2d_msgs::msg::Twist2D velocity;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(velocity);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(velocity);
  }

  bool operator==(const Twist2DStamped & other) const
  {
    return header == other.header &&
      velocity == other.velocity;
  }
  bool operator!=(const Twist2DStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_2d_msgs

#endif  // NAV_2D_MSGS_MSG_TWIST2DSTAMPED_HPP_
