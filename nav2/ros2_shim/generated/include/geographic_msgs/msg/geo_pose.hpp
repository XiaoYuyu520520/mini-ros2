#ifndef GEOGRAPHIC_MSGS_MSG_GEOPOSE_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOPOSE_HPP_

#include "geographic_msgs/msg/geo_point.hpp"
#include "geometry_msgs/msg/quaternion.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct GeoPose
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeoPose";
  using SharedPtr = std::shared_ptr<GeoPose>;
  using ConstSharedPtr = std::shared_ptr<const GeoPose>;
  using UniquePtr = std::unique_ptr<GeoPose>;
  using _position_type = ::geographic_msgs::msg::GeoPoint;
  ::geographic_msgs::msg::GeoPoint position;
  using _orientation_type = ::geometry_msgs::msg::Quaternion;
  ::geometry_msgs::msg::Quaternion orientation;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(position);
    ar(orientation);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(position);
    ar(orientation);
  }

  bool operator==(const GeoPose & other) const
  {
    return position == other.position &&
      orientation == other.orientation;
  }
  bool operator!=(const GeoPose & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOPOSE_HPP_
