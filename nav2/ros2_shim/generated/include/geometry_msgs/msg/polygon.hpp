#ifndef GEOMETRY_MSGS_MSG_POLYGON_HPP_
#define GEOMETRY_MSGS_MSG_POLYGON_HPP_

#include "geometry_msgs/msg/point32.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Polygon
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Polygon";
  using SharedPtr = std::shared_ptr<Polygon>;
  using ConstSharedPtr = std::shared_ptr<const Polygon>;
  using UniquePtr = std::unique_ptr<Polygon>;
  using _points_type = std::vector<::geometry_msgs::msg::Point32>;
  std::vector<::geometry_msgs::msg::Point32> points;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(points);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(points);
  }

  bool operator==(const Polygon & other) const
  {
    return points == other.points;
  }
  bool operator!=(const Polygon & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POLYGON_HPP_
