#ifndef SENSOR_MSGS_MSG_JOY_HPP_
#define SENSOR_MSGS_MSG_JOY_HPP_

#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct Joy
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/Joy";
  using SharedPtr = std::shared_ptr<Joy>;
  using ConstSharedPtr = std::shared_ptr<const Joy>;
  using UniquePtr = std::unique_ptr<Joy>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _axes_type = std::vector<float>;
  std::vector<float> axes;
  using _buttons_type = std::vector<int32_t>;
  std::vector<int32_t> buttons;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(axes);
    ar(buttons);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(axes);
    ar(buttons);
  }

  bool operator==(const Joy & other) const
  {
    return header == other.header &&
      axes == other.axes &&
      buttons == other.buttons;
  }
  bool operator!=(const Joy & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_JOY_HPP_
