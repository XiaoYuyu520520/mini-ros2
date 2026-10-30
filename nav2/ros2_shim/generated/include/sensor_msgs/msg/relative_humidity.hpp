#ifndef SENSOR_MSGS_MSG_RELATIVEHUMIDITY_HPP_
#define SENSOR_MSGS_MSG_RELATIVEHUMIDITY_HPP_

#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct RelativeHumidity
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/RelativeHumidity";
  using SharedPtr = std::shared_ptr<RelativeHumidity>;
  using ConstSharedPtr = std::shared_ptr<const RelativeHumidity>;
  using UniquePtr = std::unique_ptr<RelativeHumidity>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _relative_humidity_type = double;
  double relative_humidity = 0.0;
  using _variance_type = double;
  double variance = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(relative_humidity);
    ar(variance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(relative_humidity);
    ar(variance);
  }

  bool operator==(const RelativeHumidity & other) const
  {
    return header == other.header &&
      relative_humidity == other.relative_humidity &&
      variance == other.variance;
  }
  bool operator!=(const RelativeHumidity & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_RELATIVEHUMIDITY_HPP_
