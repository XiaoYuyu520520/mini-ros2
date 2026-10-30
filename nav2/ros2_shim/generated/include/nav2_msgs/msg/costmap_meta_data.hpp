#ifndef NAV2_MSGS_MSG_COSTMAPMETADATA_HPP_
#define NAV2_MSGS_MSG_COSTMAPMETADATA_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "geometry_msgs/msg/pose.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct CostmapMetaData
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/CostmapMetaData";
  using SharedPtr = std::shared_ptr<CostmapMetaData>;
  using ConstSharedPtr = std::shared_ptr<const CostmapMetaData>;
  using UniquePtr = std::unique_ptr<CostmapMetaData>;
  using _map_load_time_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time map_load_time;
  using _update_time_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time update_time;
  using _layer_type = std::string;
  std::string layer;
  using _resolution_type = float;
  float resolution = 0.0f;
  using _size_x_type = uint32_t;
  uint32_t size_x = 0;
  using _size_y_type = uint32_t;
  uint32_t size_y = 0;
  using _origin_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose origin;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(map_load_time);
    ar(update_time);
    ar(layer);
    ar(resolution);
    ar(size_x);
    ar(size_y);
    ar(origin);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(map_load_time);
    ar(update_time);
    ar(layer);
    ar(resolution);
    ar(size_x);
    ar(size_y);
    ar(origin);
  }

  bool operator==(const CostmapMetaData & other) const
  {
    return map_load_time == other.map_load_time &&
      update_time == other.update_time &&
      layer == other.layer &&
      resolution == other.resolution &&
      size_x == other.size_x &&
      size_y == other.size_y &&
      origin == other.origin;
  }
  bool operator!=(const CostmapMetaData & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_COSTMAPMETADATA_HPP_
