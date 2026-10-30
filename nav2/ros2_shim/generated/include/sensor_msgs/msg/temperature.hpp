#ifndef SENSOR_MSGS_MSG_TEMPERATURE_HPP_
#define SENSOR_MSGS_MSG_TEMPERATURE_HPP_

#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct Temperature
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/Temperature";
  using SharedPtr = std::shared_ptr<Temperature>;
  using ConstSharedPtr = std::shared_ptr<const Temperature>;
  using UniquePtr = std::unique_ptr<Temperature>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _temperature_type = double;
  double temperature = 0.0;
  using _variance_type = double;
  double variance = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(temperature);
    ar(variance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(temperature);
    ar(variance);
  }

  bool operator==(const Temperature & other) const
  {
    return header == other.header &&
      temperature == other.temperature &&
      variance == other.variance;
  }
  bool operator!=(const Temperature & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_TEMPERATURE_HPP_
