#ifndef SENSOR_MSGS_MSG_LASERECHO_HPP_
#define SENSOR_MSGS_MSG_LASERECHO_HPP_

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct LaserEcho
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/LaserEcho";
  using SharedPtr = std::shared_ptr<LaserEcho>;
  using ConstSharedPtr = std::shared_ptr<const LaserEcho>;
  using UniquePtr = std::unique_ptr<LaserEcho>;
  using _echoes_type = std::vector<float>;
  std::vector<float> echoes;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(echoes);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(echoes);
  }

  bool operator==(const LaserEcho & other) const
  {
    return echoes == other.echoes;
  }
  bool operator!=(const LaserEcho & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_LASERECHO_HPP_
