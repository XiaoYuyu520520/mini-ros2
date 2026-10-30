#ifndef SENSOR_MSGS_MSG_JOYFEEDBACKARRAY_HPP_
#define SENSOR_MSGS_MSG_JOYFEEDBACKARRAY_HPP_

#include "sensor_msgs/msg/joy_feedback.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct JoyFeedbackArray
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/JoyFeedbackArray";
  using SharedPtr = std::shared_ptr<JoyFeedbackArray>;
  using ConstSharedPtr = std::shared_ptr<const JoyFeedbackArray>;
  using UniquePtr = std::unique_ptr<JoyFeedbackArray>;
  using _array_type = std::vector<::sensor_msgs::msg::JoyFeedback>;
  std::vector<::sensor_msgs::msg::JoyFeedback> array;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(array);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(array);
  }

  bool operator==(const JoyFeedbackArray & other) const
  {
    return array == other.array;
  }
  bool operator!=(const JoyFeedbackArray & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_JOYFEEDBACKARRAY_HPP_
