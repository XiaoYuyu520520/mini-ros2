#ifndef VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERPOSE_HPP_
#define VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERPOSE_HPP_

#include "geometry_msgs/msg/pose.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct InteractiveMarkerPose
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/InteractiveMarkerPose";
  using SharedPtr = std::shared_ptr<InteractiveMarkerPose>;
  using ConstSharedPtr = std::shared_ptr<const InteractiveMarkerPose>;
  using UniquePtr = std::unique_ptr<InteractiveMarkerPose>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;
  using _name_type = std::string;
  std::string name;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(pose);
    ar(name);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(pose);
    ar(name);
  }

  bool operator==(const InteractiveMarkerPose & other) const
  {
    return header == other.header &&
      pose == other.pose &&
      name == other.name;
  }
  bool operator!=(const InteractiveMarkerPose & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERPOSE_HPP_
