#ifndef GEOMETRY_MSGS_MSG_VECTOR3STAMPED_HPP_
#define GEOMETRY_MSGS_MSG_VECTOR3STAMPED_HPP_

#include "geometry_msgs/msg/vector3.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Vector3Stamped
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Vector3Stamped";
  using SharedPtr = std::shared_ptr<Vector3Stamped>;
  using ConstSharedPtr = std::shared_ptr<const Vector3Stamped>;
  using UniquePtr = std::unique_ptr<Vector3Stamped>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _vector_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 vector;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(vector);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(vector);
  }

  bool operator==(const Vector3Stamped & other) const
  {
    return header == other.header &&
      vector == other.vector;
  }
  bool operator!=(const Vector3Stamped & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_VECTOR3STAMPED_HPP_
