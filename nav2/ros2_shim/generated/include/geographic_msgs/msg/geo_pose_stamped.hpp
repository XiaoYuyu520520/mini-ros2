#ifndef GEOGRAPHIC_MSGS_MSG_GEOPOSESTAMPED_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOPOSESTAMPED_HPP_

#include "geographic_msgs/msg/geo_pose.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct GeoPoseStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeoPoseStamped";
  using SharedPtr = std::shared_ptr<GeoPoseStamped>;
  using ConstSharedPtr = std::shared_ptr<const GeoPoseStamped>;
  using UniquePtr = std::unique_ptr<GeoPoseStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _pose_type = ::geographic_msgs::msg::GeoPose;
  ::geographic_msgs::msg::GeoPose pose;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(pose);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(pose);
  }

  bool operator==(const GeoPoseStamped & other) const
  {
    return header == other.header &&
      pose == other.pose;
  }
  bool operator!=(const GeoPoseStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOPOSESTAMPED_HPP_
