#ifndef NAV2_MSGS_MSG_VOXELGRID_HPP_
#define NAV2_MSGS_MSG_VOXELGRID_HPP_

#include "geometry_msgs/msg/point32.hpp"
#include "geometry_msgs/msg/vector3.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct VoxelGrid
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/VoxelGrid";
  using SharedPtr = std::shared_ptr<VoxelGrid>;
  using ConstSharedPtr = std::shared_ptr<const VoxelGrid>;
  using UniquePtr = std::unique_ptr<VoxelGrid>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _data_type = std::vector<uint32_t>;
  std::vector<uint32_t> data;
  using _origin_type = ::geometry_msgs::msg::Point32;
  ::geometry_msgs::msg::Point32 origin;
  using _resolutions_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 resolutions;
  using _size_x_type = uint32_t;
  uint32_t size_x = 0;
  using _size_y_type = uint32_t;
  uint32_t size_y = 0;
  using _size_z_type = uint32_t;
  uint32_t size_z = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(data);
    ar(origin);
    ar(resolutions);
    ar(size_x);
    ar(size_y);
    ar(size_z);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(data);
    ar(origin);
    ar(resolutions);
    ar(size_x);
    ar(size_y);
    ar(size_z);
  }

  bool operator==(const VoxelGrid & other) const
  {
    return header == other.header &&
      data == other.data &&
      origin == other.origin &&
      resolutions == other.resolutions &&
      size_x == other.size_x &&
      size_y == other.size_y &&
      size_z == other.size_z;
  }
  bool operator!=(const VoxelGrid & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_VOXELGRID_HPP_
