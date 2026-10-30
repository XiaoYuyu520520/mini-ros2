#ifndef GEOMETRY_MSGS_MSG_POINTSTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_POINTSTAMPED_HPP_

#include "geometry_msgs/msg/point.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct PointStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/PointStamped";
  using SharedPtr = std::shared_ptr<PointStamped>;
  using ConstSharedPtr = std::shared_ptr<const PointStamped>;
  using UniquePtr = std::unique_ptr<PointStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _point_type = ::geometry_msgs::msg::Point;
  ::geometry_msgs::msg::Point point;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(point);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(point);
  }

  bool operator==(const PointStamped & other) const
  {
    return header == other.header &&
      point == other.point;
  }
  bool operator!=(const PointStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POINTSTAMPED_HPP_
