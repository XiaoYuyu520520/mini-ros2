#ifndef GEOMETRY_MSGS_MSG_POLYGONSTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_POLYGONSTAMPED_HPP_

#include "geometry_msgs/msg/polygon.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct PolygonStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/PolygonStamped";
  using SharedPtr = std::shared_ptr<PolygonStamped>;
  using ConstSharedPtr = std::shared_ptr<const PolygonStamped>;
  using UniquePtr = std::unique_ptr<PolygonStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _polygon_type = ::geometry_msgs::msg::Polygon;
  ::geometry_msgs::msg::Polygon polygon;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(polygon);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(polygon);
  }

  bool operator==(const PolygonStamped & other) const
  {
    return header == other.header &&
      polygon == other.polygon;
  }
  bool operator!=(const PolygonStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POLYGONSTAMPED_HPP_
