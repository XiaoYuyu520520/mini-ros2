#ifndef GEOMETRY_MSGS_MSG_TRANSFORMSTAMPED_HPP_
#define GEOMETRY_MSGS_MSG_TRANSFORMSTAMPED_HPP_

#include "geometry_msgs/msg/transform.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct TransformStamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/TransformStamped";
  using SharedPtr = std::shared_ptr<TransformStamped>;
  using ConstSharedPtr = std::shared_ptr<const TransformStamped>;
  using UniquePtr = std::unique_ptr<TransformStamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _child_frame_id_type = std::string;
  std::string child_frame_id;
  using _transform_type = ::geometry_msgs::msg::Transform;
  ::geometry_msgs::msg::Transform transform;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(child_frame_id);
    ar(transform);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(child_frame_id);
    ar(transform);
  }

  bool operator==(const TransformStamped & other) const
  {
    return header == other.header &&
      child_frame_id == other.child_frame_id &&
      transform == other.transform;
  }
  bool operator!=(const TransformStamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_TRANSFORMSTAMPED_HPP_
