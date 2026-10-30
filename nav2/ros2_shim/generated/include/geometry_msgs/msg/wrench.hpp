#ifndef GEOMETRY_MSGS_MSG_WRENCH_HPP_
#define GEOMETRY_MSGS_MSG_WRENCH_HPP_

#include "geometry_msgs/msg/vector3.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Wrench
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Wrench";
  using SharedPtr = std::shared_ptr<Wrench>;
  using ConstSharedPtr = std::shared_ptr<const Wrench>;
  using UniquePtr = std::unique_ptr<Wrench>;
  using _force_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 force;
  using _torque_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 torque;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(force);
    ar(torque);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(force);
    ar(torque);
  }

  bool operator==(const Wrench & other) const
  {
    return force == other.force &&
      torque == other.torque;
  }
  bool operator!=(const Wrench & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_WRENCH_HPP_
