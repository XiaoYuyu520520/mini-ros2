#ifndef SENSOR_MSGS_MSG_MAGNETICFIELD_HPP_
#define SENSOR_MSGS_MSG_MAGNETICFIELD_HPP_

#include <array>
#include "geometry_msgs/msg/vector3.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct MagneticField
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/MagneticField";
  using SharedPtr = std::shared_ptr<MagneticField>;
  using ConstSharedPtr = std::shared_ptr<const MagneticField>;
  using UniquePtr = std::unique_ptr<MagneticField>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _magnetic_field_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 magnetic_field;
  using _magnetic_field_covariance_type = std::array<double, 9>;
  std::array<double, 9> magnetic_field_covariance = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(magnetic_field);
    ar(magnetic_field_covariance);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(magnetic_field);
    ar(magnetic_field_covariance);
  }

  bool operator==(const MagneticField & other) const
  {
    return header == other.header &&
      magnetic_field == other.magnetic_field &&
      magnetic_field_covariance == other.magnetic_field_covariance;
  }
  bool operator!=(const MagneticField & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_MAGNETICFIELD_HPP_
