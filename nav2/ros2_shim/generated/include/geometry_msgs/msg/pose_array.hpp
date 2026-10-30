#ifndef GEOMETRY_MSGS_MSG_POSEARRAY_HPP_
#define GEOMETRY_MSGS_MSG_POSEARRAY_HPP_

#include "geometry_msgs/msg/pose.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct PoseArray
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/PoseArray";
  using SharedPtr = std::shared_ptr<PoseArray>;
  using ConstSharedPtr = std::shared_ptr<const PoseArray>;
  using UniquePtr = std::unique_ptr<PoseArray>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _poses_type = std::vector<::geometry_msgs::msg::Pose>;
  std::vector<::geometry_msgs::msg::Pose> poses;

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

  bool operator==(const PoseArray & other) const
  {
    return header == other.header &&
      poses == other.poses;
  }
  bool operator!=(const PoseArray & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POSEARRAY_HPP_
