#ifndef GEOMETRY_MSGS_MSG_QUATERNIONSTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_QUATERNIONSTAMPED_HPP_

#include "geometry_msgs/msg/quaternion.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct QuaternionStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/QuaternionStamped";
  using SharedPtr = std::shared_ptr<QuaternionStamped>;
  using ConstSharedPtr = std::shared_ptr<const QuaternionStamped>;
  using UniquePtr = std::unique_ptr<QuaternionStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _quaternion_type = ::geometry_msgs::msg::Quaternion;
  ::geometry_msgs::msg::Quaternion quaternion;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(quaternion);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(quaternion);
  }

  bool operator==(const QuaternionStamped & other) const
  {
    return header == other.header &&
      quaternion == other.quaternion;
  }
  bool operator!=(const QuaternionStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_QUATERNIONSTAMPED_HPP_
