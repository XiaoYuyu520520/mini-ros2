#ifndef NAV2_MSGS_MSG_PARTICLE_HPP_
#define NAV2_MSGS_MSG_PARTICLE_HPP_

#include "geometry_msgs/msg/pose.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct Particle
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/Particle";
  using SharedPtr = std::shared_ptr<Particle>;
  using ConstSharedPtr = std::shared_ptr<const Particle>;
  using UniquePtr = std::unique_ptr<Particle>;
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;
  using _weight_type = double;
  double weight = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(pose);
    ar(weight);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(pose);
    ar(weight);
  }

  bool operator==(const Particle & other) const
  {
    return pose == other.pose &&
      weight == other.weight;
  }
  bool operator!=(const Particle & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_PARTICLE_HPP_
