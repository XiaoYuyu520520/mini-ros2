#ifndef NAV_MSGS_MSG_MAPMETADATA_HPP_
#define NAV_MSGS_MSG_MAPMETADATA_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "geometry_msgs/msg/pose.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct MapMetaData
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/MapMetaData";
  using SharedPtr = std::shared_ptr<MapMetaData>;
  using ConstSharedPtr = std::shared_ptr<const MapMetaData>;
  using UniquePtr = std::unique_ptr<MapMetaData>;
  using _map_load_time_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time map_load_time;
  using _resolution_type = float;
  float resolution = 0.0f;
  using _width_type = uint32_t;
  uint32_t width = 0;
  using _height_type = uint32_t;
  uint32_t height = 0;
  using _origin_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose origin;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(map_load_time);
    ar(resolution);
    ar(width);
    ar(height);
    ar(origin);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(map_load_time);
    ar(resolution);
    ar(width);
    ar(height);
    ar(origin);
  }

  bool operator==(const MapMetaData & other) const
  {
    return map_load_time == other.map_load_time &&
      resolution == other.resolution &&
      width == other.width &&
      height == other.height &&
      origin == other.origin;
  }
  bool operator!=(const MapMetaData & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_MAPMETADATA_HPP_
