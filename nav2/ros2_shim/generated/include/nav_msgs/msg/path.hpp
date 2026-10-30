#ifndef NAV_MSGS_MSG_PATH_HPP_
#define NAV_MSGS_MSG_PATH_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct Path
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/Path";
  using SharedPtr = std::shared_ptr<Path>;
  using ConstSharedPtr = std::shared_ptr<const Path>;
  using UniquePtr = std::unique_ptr<Path>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _poses_type = std::vector<::geometry_msgs::msg::PoseStamped>;
  std::vector<::geometry_msgs::msg::PoseStamped> poses;

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

  bool operator==(const Path & other) const
  {
    return header == other.header &&
      poses == other.poses;
  }
  bool operator!=(const Path & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_PATH_HPP_
