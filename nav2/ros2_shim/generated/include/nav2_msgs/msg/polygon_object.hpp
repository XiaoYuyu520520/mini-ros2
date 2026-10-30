#ifndef NAV2_MSGS_MSG_POLYGONOBJECT_HPP_
#define NAV2_MSGS_MSG_POLYGONOBJECT_HPP_

#include "geometry_msgs/msg/point32.hpp"
#include "std_msgs/msg/header.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct PolygonObject
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/PolygonObject";
  using SharedPtr = std::shared_ptr<PolygonObject>;
  using ConstSharedPtr = std::shared_ptr<const PolygonObject>;
  using UniquePtr = std::unique_ptr<PolygonObject>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _uuid_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID uuid;
  using _points_type = std::vector<::geometry_msgs::msg::Point32>;
  std::vector<::geometry_msgs::msg::Point32> points;
  using _closed_type = bool;
  bool closed = false;
  using _value_type = int8_t;
  int8_t value = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(uuid);
    ar(points);
    ar(closed);
    ar(value);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(uuid);
    ar(points);
    ar(closed);
    ar(value);
  }

  bool operator==(const PolygonObject & other) const
  {
    return header == other.header &&
      uuid == other.uuid &&
      points == other.points &&
      closed == other.closed &&
      value == other.value;
  }
  bool operator!=(const PolygonObject & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_POLYGONOBJECT_HPP_
