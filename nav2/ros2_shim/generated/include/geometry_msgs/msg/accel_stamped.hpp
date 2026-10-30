#ifndef GEOMETRY_MSGS_MSG_ACCELSTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_ACCELSTAMPED_HPP_

#include "geometry_msgs/msg/accel.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct AccelStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/AccelStamped";
  using SharedPtr = std::shared_ptr<AccelStamped>;
  using ConstSharedPtr = std::shared_ptr<const AccelStamped>;
  using UniquePtr = std::unique_ptr<AccelStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _accel_type = ::geometry_msgs::msg::Accel;
  ::geometry_msgs::msg::Accel accel;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(accel);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(accel);
  }

  bool operator==(const AccelStamped & other) const
  {
    return header == other.header &&
      accel == other.accel;
  }
  bool operator!=(const AccelStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_ACCELSTAMPED_HPP_
