#ifndef NAV_MSGS_MSG_OCCUPANCYGRID_HPP_
#define NAV_MSGS_MSG_OCCUPANCYGRID_HPP_

#include "nav_msgs/msg/map_meta_data.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct OccupancyGrid
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/OccupancyGrid";
  using SharedPtr = std::shared_ptr<OccupancyGrid>;
  using ConstSharedPtr = std::shared_ptr<const OccupancyGrid>;
  using UniquePtr = std::unique_ptr<OccupancyGrid>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _info_type = ::nav_msgs::msg::MapMetaData;
  ::nav_msgs::msg::MapMetaData info;
  using _data_type = std::vector<int8_t>;
  std::vector<int8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(info);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(info);
    ar(data);
  }

  bool operator==(const OccupancyGrid & other) const
  {
    return header == other.header &&
      info == other.info &&
      data == other.data;
  }
  bool operator!=(const OccupancyGrid & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_OCCUPANCYGRID_HPP_
