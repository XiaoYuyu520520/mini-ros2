#ifndef NAV2_MSGS_MSG_COSTMAPFILTERINFO_HPP_
#define NAV2_MSGS_MSG_COSTMAPFILTERINFO_HPP_

#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct CostmapFilterInfo
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/CostmapFilterInfo";
  using SharedPtr = std::shared_ptr<CostmapFilterInfo>;
  using ConstSharedPtr = std::shared_ptr<const CostmapFilterInfo>;
  using UniquePtr = std::unique_ptr<CostmapFilterInfo>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _type_type = uint8_t;
  uint8_t type = 0;
  using _filter_mask_topic_type = std::string;
  std::string filter_mask_topic;
  using _base_type = float;
  float base = 0.0f;
  using _multiplier_type = float;
  float multiplier = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(type);
    ar(filter_mask_topic);
    ar(base);
    ar(multiplier);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(type);
    ar(filter_mask_topic);
    ar(base);
    ar(multiplier);
  }

  bool operator==(const CostmapFilterInfo & other) const
  {
    return header == other.header &&
      type == other.type &&
      filter_mask_topic == other.filter_mask_topic &&
      base == other.base &&
      multiplier == other.multiplier;
  }
  bool operator!=(const CostmapFilterInfo & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_COSTMAPFILTERINFO_HPP_
