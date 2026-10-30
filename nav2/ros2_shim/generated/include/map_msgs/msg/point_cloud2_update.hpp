#ifndef MAP_MSGS_MSG_POINTCLOUD2UPDATE_HPP_
#define MAP_MSGS_MSG_POINTCLOUD2UPDATE_HPP_

#include "sensor_msgs/msg/point_cloud2.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

// These constants share their names with macros a platform header defines
// (`ERROR` is one in `windows.h`), and a declaration is rewritten by the macro
// rather than compiled when one is in scope. Dropped here, once, instead of in
// every translation unit that includes both.
#ifdef DELETE
#  undef DELETE
#endif

namespace map_msgs
{
namespace msg
{

struct PointCloud2Update
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "map_msgs/msg/PointCloud2Update";
  using SharedPtr = std::shared_ptr<PointCloud2Update>;
  using ConstSharedPtr = std::shared_ptr<const PointCloud2Update>;
  using UniquePtr = std::unique_ptr<PointCloud2Update>;
  static constexpr uint32_t ADD = 0;
  static constexpr uint32_t DELETE = 1;

  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _type_type = uint32_t;
  uint32_t type = 0;
  using _points_type = ::sensor_msgs::msg::PointCloud2;
  ::sensor_msgs::msg::PointCloud2 points;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(type);
    ar(points);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(type);
    ar(points);
  }

  bool operator==(const PointCloud2Update & other) const
  {
    return header == other.header &&
      type == other.type &&
      points == other.points;
  }
  bool operator!=(const PointCloud2Update & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace map_msgs

#endif  // MAP_MSGS_MSG_POINTCLOUD2UPDATE_HPP_
