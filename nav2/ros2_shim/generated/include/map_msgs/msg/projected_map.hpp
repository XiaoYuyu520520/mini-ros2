#ifndef MAP_MSGS_MSG_PROJECTEDMAP_HPP_
#define MAP_MSGS_MSG_PROJECTEDMAP_HPP_

#include "nav_msgs/msg/occupancy_grid.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace msg
{

struct ProjectedMap
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "map_msgs/msg/ProjectedMap";
  using SharedPtr = std::shared_ptr<ProjectedMap>;
  using ConstSharedPtr = std::shared_ptr<const ProjectedMap>;
  using UniquePtr = std::unique_ptr<ProjectedMap>;
  using _map_type = ::nav_msgs::msg::OccupancyGrid;
  ::nav_msgs::msg::OccupancyGrid map;
  using _min_z_type = double;
  double min_z = 0.0;
  using _max_z_type = double;
  double max_z = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(map);
    ar(min_z);
    ar(max_z);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(map);
    ar(min_z);
    ar(max_z);
  }

  bool operator==(const ProjectedMap & other) const
  {
    return map == other.map &&
      min_z == other.min_z &&
      max_z == other.max_z;
  }
  bool operator!=(const ProjectedMap & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace map_msgs

#endif  // MAP_MSGS_MSG_PROJECTEDMAP_HPP_
