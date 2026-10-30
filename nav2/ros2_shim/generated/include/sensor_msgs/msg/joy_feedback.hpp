#ifndef SENSOR_MSGS_MSG_JOYFEEDBACK_HPP_
#define SENSOR_MSGS_MSG_JOYFEEDBACK_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct JoyFeedback
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/JoyFeedback";
  using SharedPtr = std::shared_ptr<JoyFeedback>;
  using ConstSharedPtr = std::shared_ptr<const JoyFeedback>;
  using UniquePtr = std::unique_ptr<JoyFeedback>;
  static constexpr uint8_t TYPE_LED = 0;
  static constexpr uint8_t TYPE_RUMBLE = 1;
  static constexpr uint8_t TYPE_BUZZER = 2;

  using _type_type = uint8_t;
  uint8_t type = 0;
  using _id_type = uint8_t;
  uint8_t id = 0;
  using _intensity_type = float;
  float intensity = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(type);
    ar(id);
    ar(intensity);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(type);
    ar(id);
    ar(intensity);
  }

  bool operator==(const JoyFeedback & other) const
  {
    return type == other.type &&
      id == other.id &&
      intensity == other.intensity;
  }
  bool operator!=(const JoyFeedback & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_JOYFEEDBACK_HPP_
