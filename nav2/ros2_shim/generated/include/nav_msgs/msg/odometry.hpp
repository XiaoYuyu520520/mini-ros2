#ifndef NAV_MSGS_MSG_ODOMETRY_HPP_
#define NAV_MSGS_MSG_ODOMETRY_HPP_

#include "geometry_msgs/msg/pose_with_covariance.hpp"
#include "geometry_msgs/msg/twist_with_covariance.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct Odometry
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/Odometry";
  using SharedPtr = std::shared_ptr<Odometry>;
  using ConstSharedPtr = std::shared_ptr<const Odometry>;
  using UniquePtr = std::unique_ptr<Odometry>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _child_frame_id_type = std::string;
  std::string child_frame_id;
  using _pose_type = ::geometry_msgs::msg::PoseWithCovariance;
  ::geometry_msgs::msg::PoseWithCovariance pose;
  using _twist_type = ::geometry_msgs::msg::TwistWithCovariance;
  ::geometry_msgs::msg::TwistWithCovariance twist;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(child_frame_id);
    ar(pose);
    ar(twist);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(child_frame_id);
    ar(pose);
    ar(twist);
  }

  bool operator==(const Odometry & other) const
  {
    return header == other.header &&
      child_frame_id == other.child_frame_id &&
      pose == other.pose &&
      twist == other.twist;
  }
  bool operator!=(const Odometry & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_ODOMETRY_HPP_
