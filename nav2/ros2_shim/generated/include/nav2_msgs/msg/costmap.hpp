#ifndef NAV2_MSGS_MSG_COSTMAP_HPP_
#define NAV2_MSGS_MSG_COSTMAP_HPP_

#include "nav2_msgs/msg/costmap_meta_data.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct Costmap
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/Costmap";
  using SharedPtr = std::shared_ptr<Costmap>;
  using ConstSharedPtr = std::shared_ptr<const Costmap>;
  using UniquePtr = std::unique_ptr<Costmap>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _metadata_type = ::nav2_msgs::msg::CostmapMetaData;
  ::nav2_msgs::msg::CostmapMetaData metadata;
  using _data_type = std::vector<uint8_t>;
  std::vector<uint8_t> data;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(metadata);
    ar(data);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(metadata);
    ar(data);
  }

  bool operator==(const Costmap & other) const
  {
    return header == other.header &&
      metadata == other.metadata &&
      data == other.data;
  }
  bool operator!=(const Costmap & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_COSTMAP_HPP_
