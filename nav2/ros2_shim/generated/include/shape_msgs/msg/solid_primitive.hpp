#ifndef SHAPE_MSGS_MSG_SOLIDPRIMITIVE_HPP_
#define SHAPE_MSGS_MSG_SOLIDPRIMITIVE_HPP_

#include "geometry_msgs/msg/polygon.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace shape_msgs
{
namespace msg
{

struct SolidPrimitive
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "shape_msgs/msg/SolidPrimitive";
  using SharedPtr = std::shared_ptr<SolidPrimitive>;
  using ConstSharedPtr = std::shared_ptr<const SolidPrimitive>;
  using UniquePtr = std::unique_ptr<SolidPrimitive>;
  static constexpr uint8_t BOX = 1;
  static constexpr uint8_t SPHERE = 2;
  static constexpr uint8_t CYLINDER = 3;
  static constexpr uint8_t CONE = 4;
  static constexpr uint8_t PRISM = 5;
  static constexpr uint8_t BOX_X = 0;
  static constexpr uint8_t BOX_Y = 1;
  static constexpr uint8_t BOX_Z = 2;
  static constexpr uint8_t SPHERE_RADIUS = 0;
  static constexpr uint8_t CYLINDER_HEIGHT = 0;
  static constexpr uint8_t CYLINDER_RADIUS = 1;
  static constexpr uint8_t CONE_HEIGHT = 0;
  static constexpr uint8_t CONE_RADIUS = 1;
  static constexpr uint8_t PRISM_HEIGHT = 0;

  using _type_type = uint8_t;
  uint8_t type = 0;
  using _dimensions_type = std::vector<double>;
  std::vector<double> dimensions;
  using _polygon_type = ::geometry_msgs::msg::Polygon;
  ::geometry_msgs::msg::Polygon polygon;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(type);
    ar(dimensions);
    ar(polygon);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(type);
    ar(dimensions);
    ar(polygon);
  }

  bool operator==(const SolidPrimitive & other) const
  {
    return type == other.type &&
      dimensions == other.dimensions &&
      polygon == other.polygon;
  }
  bool operator!=(const SolidPrimitive & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace shape_msgs

#endif  // SHAPE_MSGS_MSG_SOLIDPRIMITIVE_HPP_
