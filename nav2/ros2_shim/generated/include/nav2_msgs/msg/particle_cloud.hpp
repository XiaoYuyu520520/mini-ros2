#ifndef NAV2_MSGS_MSG_PARTICLECLOUD_HPP_
#define NAV2_MSGS_MSG_PARTICLECLOUD_HPP_

#include "nav2_msgs/msg/particle.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct ParticleCloud
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/ParticleCloud";
  using SharedPtr = std::shared_ptr<ParticleCloud>;
  using ConstSharedPtr = std::shared_ptr<const ParticleCloud>;
  using UniquePtr = std::unique_ptr<ParticleCloud>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _particles_type = std::vector<::nav2_msgs::msg::Particle>;
  std::vector<::nav2_msgs::msg::Particle> particles;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(particles);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(particles);
  }

  bool operator==(const ParticleCloud & other) const
  {
    return header == other.header &&
      particles == other.particles;
  }
  bool operator!=(const ParticleCloud & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_PARTICLECLOUD_HPP_
