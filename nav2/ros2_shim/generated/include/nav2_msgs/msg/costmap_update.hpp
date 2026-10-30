#ifndef NAV2_MSGS_MSG_COSTMAPUPDATE_HPP_
#define NAV2_MSGS_MSG_COSTMAPUPDATE_HPP_

#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct CostmapUpdate
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/CostmapUpdate";
  using SharedPtr = std::shared_ptr<CostmapUpdate>;
  using ConstSharedPtr = std::shared_ptr<const CostmapUpdate>;
  using UniquePtr = std::unique_ptr<CostmapUpdate>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _x_type = uint32_t;
  uint32_t x = 0;
  using _y_type = uint32_t;
  uint32_t y = 0;
  using _size_x_type = uint32_t;
  uint32_t size_x = 0;
  using _size_y_type = uint32_t;
  uint32_t size_y = 0;
  using _data_type = std::vector<uint8_t>;
  std::vector<uint8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(x);
    ar(y);
    ar(size_x);
    ar(size_y);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(x);
    ar(y);
    ar(size_x);
    ar(size_y);
    ar(data);
  }

  bool operator==(const CostmapUpdate & other) const
  {
    return header == other.header &&
      x == other.x &&
      y == other.y &&
      size_x == other.size_x &&
      size_y == other.size_y &&
      data == other.data;
  }
  bool operator!=(const CostmapUpdate & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_COSTMAPUPDATE_HPP_
