#ifndef GEOMETRY_MSGS_MSG_TRANSFORM_HPP_
#define GEOMETRY_MSGS_MSG_TRANSFORM_HPP_

#include "geometry_msgs/msg/quaternion.hpp"
#include "geometry_msgs/msg/vector3.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Transform
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Transform";
  using SharedPtr = std::shared_ptr<Transform>;
  using ConstSharedPtr = std::shared_ptr<const Transform>;
  using UniquePtr = std::unique_ptr<Transform>;
  using _translation_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 translation;
  using _rotation_type = ::geometry_msgs::msg::Quaternion;
  ::geometry_msgs::msg::Quaternion rotation;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(translation);
    ar(rotation);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(translation);
    ar(rotation);
  }

  bool operator==(const Transform & other) const
  {
    return translation == other.translation &&
      rotation == other.rotation;
  }
  bool operator!=(const Transform & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_TRANSFORM_HPP_
