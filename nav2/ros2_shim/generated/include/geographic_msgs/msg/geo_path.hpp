#ifndef GEOGRAPHIC_MSGS_MSG_GEOPATH_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOPATH_HPP_

#include "geographic_msgs/msg/geo_pose_stamped.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct GeoPath
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeoPath";
  using SharedPtr = std::shared_ptr<GeoPath>;
  using ConstSharedPtr = std::shared_ptr<const GeoPath>;
  using UniquePtr = std::unique_ptr<GeoPath>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _poses_type = std::vector<::geographic_msgs::msg::GeoPoseStamped>;
  std::vector<::geographic_msgs::msg::GeoPoseStamped> poses;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(poses);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(poses);
  }

  bool operator==(const GeoPath & other) const
  {
    return header == other.header &&
      poses == other.poses;
  }
  bool operator!=(const GeoPath & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOPATH_HPP_
