#ifndef GEOMETRY_MSGS_MSG_POLYGONINSTANCE_HPP_
#define GEOMETRY_MSGS_MSG_POLYGONINSTANCE_HPP_

#include "geometry_msgs/msg/polygon.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct PolygonInstance
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/PolygonInstance";
  using SharedPtr = std::shared_ptr<PolygonInstance>;
  using ConstSharedPtr = std::shared_ptr<const PolygonInstance>;
  using UniquePtr = std::unique_ptr<PolygonInstance>;
  using _polygon_type = ::geometry_msgs::msg::Polygon;
  ::geometry_msgs::msg::Polygon polygon;
  using _id_type = int64_t;
  int64_t id = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(polygon);
    ar(id);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(polygon);
    ar(id);
  }

  bool operator==(const PolygonInstance & other) const
  {
    return polygon == other.polygon &&
      id == other.id;
  }
  bool operator!=(const PolygonInstance & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POLYGONINSTANCE_HPP_
