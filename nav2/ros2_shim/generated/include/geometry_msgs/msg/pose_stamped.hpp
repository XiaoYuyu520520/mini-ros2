#ifndef GEOMETRY_MSGS_MSG_POSESTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_POSESTAMPED_HPP_

#include "geometry_msgs/msg/pose.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct PoseStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/PoseStamped";
  using SharedPtr = std::shared_ptr<PoseStamped>;
  using ConstSharedPtr = std::shared_ptr<const PoseStamped>;
  using UniquePtr = std::unique_ptr<PoseStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;

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

  bool operator==(const PoseStamped & other) const
  {
    return header == other.header &&
      pose == other.pose;
  }
  bool operator!=(const PoseStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POSESTAMPED_HPP_
