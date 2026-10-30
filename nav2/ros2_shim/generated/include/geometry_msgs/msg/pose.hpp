#ifndef GEOMETRY_MSGS_MSG_POSE_HPP_
#define GEOMETRY_MSGS_MSG_POSE_HPP_

#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/quaternion.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Pose
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Pose";
  using SharedPtr = std::shared_ptr<Pose>;
  using ConstSharedPtr = std::shared_ptr<const Pose>;
  using UniquePtr = std::unique_ptr<Pose>;
  using _position_type = ::geometry_msgs::msg::Point;
  ::geometry_msgs::msg::Point position;
  using _orientation_type = ::geometry_msgs::msg::Quaternion;
  ::geometry_msgs::msg::Quaternion orientation;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(position);
    ar(orientation);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(position);
    ar(orientation);
  }

  bool operator==(const Pose & other) const
  {
    return position == other.position &&
      orientation == other.orientation;
  }
  bool operator!=(const Pose & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POSE_HPP_
