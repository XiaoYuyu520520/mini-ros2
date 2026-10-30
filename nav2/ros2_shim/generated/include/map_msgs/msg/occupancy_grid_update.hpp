#ifndef MAP_MSGS_MSG_OCCUPANCYGRIDUPDATE_HPP_
#define MAP_MSGS_MSG_OCCUPANCYGRIDUPDATE_HPP_

#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace map_msgs
{
namespace msg
{

struct OccupancyGridUpdate
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "map_msgs/msg/OccupancyGridUpdate";
  using SharedPtr = std::shared_ptr<OccupancyGridUpdate>;
  using ConstSharedPtr = std::shared_ptr<const OccupancyGridUpdate>;
  using UniquePtr = std::unique_ptr<OccupancyGridUpdate>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _x_type = int32_t;
  int32_t x = 0;
  using _y_type = int32_t;
  int32_t y = 0;
  using _width_type = uint32_t;
  uint32_t width = 0;
  using _height_type = uint32_t;
  uint32_t height = 0;
  using _data_type = std::vector<int8_t>;
  std::vector<int8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(x);
    ar(y);
    ar(width);
    ar(height);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(x);
    ar(y);
    ar(width);
    ar(height);
    ar(data);
  }

  bool operator==(const OccupancyGridUpdate & other) const
  {
    return header == other.header &&
      x == other.x &&
      y == other.y &&
      width == other.width &&
      height == other.height &&
      data == other.data;
  }
  bool operator!=(const OccupancyGridUpdate & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace map_msgs

#endif  // MAP_MSGS_MSG_OCCUPANCYGRIDUPDATE_HPP_
