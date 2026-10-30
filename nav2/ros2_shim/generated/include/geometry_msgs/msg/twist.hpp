#ifndef GEOMETRY_MSGS_MSG_TWIST_HPP_
#define GEOMETRY_MSGS_MSG_TWIST_HPP_

#include "geometry_msgs/msg/vector3.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Twist
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Twist";
  using SharedPtr = std::shared_ptr<Twist>;
  using ConstSharedPtr = std::shared_ptr<const Twist>;
  using UniquePtr = std::unique_ptr<Twist>;
  using _linear_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 linear;
  using _angular_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 angular;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(linear);
    ar(angular);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(linear);
    ar(angular);
  }

  bool operator==(const Twist & other) const
  {
    return linear == other.linear &&
      angular == other.angular;
  }
  bool operator!=(const Twist & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_TWIST_HPP_
