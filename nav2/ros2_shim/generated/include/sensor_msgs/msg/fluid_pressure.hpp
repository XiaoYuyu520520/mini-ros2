#ifndef SENSOR_MSGS_MSG_FLUIDPRESSURE_HPP_
#define SENSOR_MSGS_MSG_FLUIDPRESSURE_HPP_

#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct FluidPressure
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/FluidPressure";
  using SharedPtr = std::shared_ptr<FluidPressure>;
  using ConstSharedPtr = std::shared_ptr<const FluidPressure>;
  using UniquePtr = std::unique_ptr<FluidPressure>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _fluid_pressure_type = double;
  double fluid_pressure = 0.0;
  using _variance_type = double;
  double variance = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(fluid_pressure);
    ar(variance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(fluid_pressure);
    ar(variance);
  }

  bool operator==(const FluidPressure & other) const
  {
    return header == other.header &&
      fluid_pressure == other.fluid_pressure &&
      variance == other.variance;
  }
  bool operator!=(const FluidPressure & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_FLUIDPRESSURE_HPP_
