#ifndef SENSOR_MSGS_MSG_ILLUMINANCE_HPP_
#define SENSOR_MSGS_MSG_ILLUMINANCE_HPP_

#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct Illuminance
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/Illuminance";
  using SharedPtr = std::shared_ptr<Illuminance>;
  using ConstSharedPtr = std::shared_ptr<const Illuminance>;
  using UniquePtr = std::unique_ptr<Illuminance>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _illuminance_type = double;
  double illuminance = 0.0;
  using _variance_type = double;
  double variance = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(illuminance);
    ar(variance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(illuminance);
    ar(variance);
  }

  bool operator==(const Illuminance & other) const
  {
    return header == other.header &&
      illuminance == other.illuminance &&
      variance == other.variance;
  }
  bool operator!=(const Illuminance & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_ILLUMINANCE_HPP_
