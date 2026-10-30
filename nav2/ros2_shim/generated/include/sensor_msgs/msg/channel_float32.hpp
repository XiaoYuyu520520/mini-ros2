#ifndef SENSOR_MSGS_MSG_CHANNELFLOAT32_HPP_
#define SENSOR_MSGS_MSG_CHANNELFLOAT32_HPP_

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct ChannelFloat32
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/ChannelFloat32";
  using SharedPtr = std::shared_ptr<ChannelFloat32>;
  using ConstSharedPtr = std::shared_ptr<const ChannelFloat32>;
  using UniquePtr = std::unique_ptr<ChannelFloat32>;
  using _name_type = std::string;
  std::string name;
  using _values_type = std::vector<float>;
  std::vector<float> values;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(values);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(values);
  }

  bool operator==(const ChannelFloat32 & other) const
  {
    return name == other.name &&
      values == other.values;
  }
  bool operator!=(const ChannelFloat32 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_CHANNELFLOAT32_HPP_
