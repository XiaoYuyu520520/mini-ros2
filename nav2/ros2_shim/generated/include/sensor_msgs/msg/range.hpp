#ifndef SENSOR_MSGS_MSG_RANGE_HPP_
#define SENSOR_MSGS_MSG_RANGE_HPP_

#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct Range
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/Range";
  using SharedPtr = std::shared_ptr<Range>;
  using ConstSharedPtr = std::shared_ptr<const Range>;
  using UniquePtr = std::unique_ptr<Range>;
  static constexpr uint8_t ULTRASOUND = 0;
  static constexpr uint8_t INFRARED = 1;

  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _radiation_type_type = uint8_t;
  uint8_t radiation_type = 0;
  using _field_of_view_type = float;
  float field_of_view = 0.0f;
  using _min_range_type = float;
  float min_range = 0.0f;
  using _max_range_type = float;
  float max_range = 0.0f;
  using _range_type = float;
  float range = 0.0f;
  using _variance_type = float;
  float variance = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(radiation_type);
    ar(field_of_view);
    ar(min_range);
    ar(max_range);
    ar(range);
    ar(variance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(radiation_type);
    ar(field_of_view);
    ar(min_range);
    ar(max_range);
    ar(range);
    ar(variance);
  }

  bool operator==(const Range & other) const
  {
    return header == other.header &&
      radiation_type == other.radiation_type &&
      field_of_view == other.field_of_view &&
      min_range == other.min_range &&
      max_range == other.max_range &&
      range == other.range &&
      variance == other.variance;
  }
  bool operator!=(const Range & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_RANGE_HPP_
