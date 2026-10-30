#ifndef MAP_MSGS_MSG_PROJECTEDMAPINFO_HPP_
#define MAP_MSGS_MSG_PROJECTEDMAPINFO_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace msg
{

struct ProjectedMapInfo
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "map_msgs/msg/ProjectedMapInfo";
  using SharedPtr = std::shared_ptr<ProjectedMapInfo>;
  using ConstSharedPtr = std::shared_ptr<const ProjectedMapInfo>;
  using UniquePtr = std::unique_ptr<ProjectedMapInfo>;
  using _frame_id_type = std::string;
  std::string frame_id;
  using _x_type = double;
  double x = 0.0;
  using _y_type = double;
  double y = 0.0;
  using _width_type = double;
  double width = 0.0;
  using _height_type = double;
  double height = 0.0;
  using _min_z_type = double;
  double min_z = 0.0;
  using _max_z_type = double;
  double max_z = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(frame_id);
    ar(x);
    ar(y);
    ar(width);
    ar(height);
    ar(min_z);
    ar(max_z);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(frame_id);
    ar(x);
    ar(y);
    ar(width);
    ar(height);
    ar(min_z);
    ar(max_z);
  }

  bool operator==(const ProjectedMapInfo & other) const
  {
    return frame_id == other.frame_id &&
      x == other.x &&
      y == other.y &&
      width == other.width &&
      height == other.height &&
      min_z == other.min_z &&
      max_z == other.max_z;
  }
  bool operator!=(const ProjectedMapInfo & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace map_msgs

#endif  // MAP_MSGS_MSG_PROJECTEDMAPINFO_HPP_
