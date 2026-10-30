#ifndef GEOGRAPHIC_MSGS_MSG_GEOGRAPHICMAPCHANGES_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOGRAPHICMAPCHANGES_HPP_

#include "geographic_msgs/msg/geographic_map.hpp"
#include "std_msgs/msg/header.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct GeographicMapChanges
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeographicMapChanges";
  using SharedPtr = std::shared_ptr<GeographicMapChanges>;
  using ConstSharedPtr = std::shared_ptr<const GeographicMapChanges>;
  using UniquePtr = std::unique_ptr<GeographicMapChanges>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _diffs_type = ::geographic_msgs::msg::GeographicMap;
  ::geographic_msgs::msg::GeographicMap diffs;
  using _deletes_type = std::vector<::unique_identifier_msgs::msg::UUID>;
  std::vector<::unique_identifier_msgs::msg::UUID> deletes;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(diffs);
    ar(deletes);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(diffs);
    ar(deletes);
  }

  bool operator==(const GeographicMapChanges & other) const
  {
    return header == other.header &&
      diffs == other.diffs &&
      deletes == other.deletes;
  }
  bool operator!=(const GeographicMapChanges & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOGRAPHICMAPCHANGES_HPP_
