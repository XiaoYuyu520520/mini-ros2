#ifndef GEOGRAPHIC_MSGS_MSG_GEOPOINTSTAMPED_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOPOINTSTAMPED_HPP_

#include "geographic_msgs/msg/geo_point.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct GeoPointStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeoPointStamped";
  using SharedPtr = std::shared_ptr<GeoPointStamped>;
  using ConstSharedPtr = std::shared_ptr<const GeoPointStamped>;
  using UniquePtr = std::unique_ptr<GeoPointStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _position_type = ::geographic_msgs::msg::GeoPoint;
  ::geographic_msgs::msg::GeoPoint position;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(position);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(position);
  }

  bool operator==(const GeoPointStamped & other) const
  {
    return header == other.header &&
      position == other.position;
  }
  bool operator!=(const GeoPointStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOPOINTSTAMPED_HPP_
