#ifndef NAV2_MSGS_MSG_CIRCLEOBJECT_HPP_
#define NAV2_MSGS_MSG_CIRCLEOBJECT_HPP_

#include "geometry_msgs/msg/point32.hpp"
#include "std_msgs/msg/header.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct CircleObject
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/CircleObject";
  using SharedPtr = std::shared_ptr<CircleObject>;
  using ConstSharedPtr = std::shared_ptr<const CircleObject>;
  using UniquePtr = std::unique_ptr<CircleObject>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _uuid_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID uuid;
  using _center_type = ::geometry_msgs::msg::Point32;
  ::geometry_msgs::msg::Point32 center;
  using _radius_type = float;
  float radius = 0.0f;
  using _fill_type = bool;
  bool fill = false;
  using _value_type = int8_t;
  int8_t value = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(uuid);
    ar(center);
    ar(radius);
    ar(fill);
    ar(value);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(uuid);
    ar(center);
    ar(radius);
    ar(fill);
    ar(value);
  }

  bool operator==(const CircleObject & other) const
  {
    return header == other.header &&
      uuid == other.uuid &&
      center == other.center &&
      radius == other.radius &&
      fill == other.fill &&
      value == other.value;
  }
  bool operator!=(const CircleObject & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_CIRCLEOBJECT_HPP_
