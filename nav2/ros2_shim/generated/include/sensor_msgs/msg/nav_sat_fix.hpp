#ifndef SENSOR_MSGS_MSG_NAVSATFIX_HPP_
#define SENSOR_MSGS_MSG_NAVSATFIX_HPP_

#include <array>
#include "sensor_msgs/msg/nav_sat_status.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct NavSatFix
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/NavSatFix";
  using SharedPtr = std::shared_ptr<NavSatFix>;
  using ConstSharedPtr = std::shared_ptr<const NavSatFix>;
  using UniquePtr = std::unique_ptr<NavSatFix>;
  static constexpr uint8_t COVARIANCE_TYPE_UNKNOWN = 0;
  static constexpr uint8_t COVARIANCE_TYPE_APPROXIMATED = 1;
  static constexpr uint8_t COVARIANCE_TYPE_DIAGONAL_KNOWN = 2;
  static constexpr uint8_t COVARIANCE_TYPE_KNOWN = 3;

  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _status_type = ::sensor_msgs::msg::NavSatStatus;
  ::sensor_msgs::msg::NavSatStatus status;
  using _latitude_type = double;
  double latitude = 0.0;
  using _longitude_type = double;
  double longitude = 0.0;
  using _altitude_type = double;
  double altitude = 0.0;
  using _position_covariance_type = std::array<double, 9>;
  std::array<double, 9> position_covariance = {};
  using _position_covariance_type_type = uint8_t;
  uint8_t position_covariance_type = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(status);
    ar(latitude);
    ar(longitude);
    ar(altitude);
    ar(position_covariance);
    ar(position_covariance_type);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(status);
    ar(latitude);
    ar(longitude);
    ar(altitude);
    ar(position_covariance);
    ar(position_covariance_type);
  }

  bool operator==(const NavSatFix & other) const
  {
    return header == other.header &&
      status == other.status &&
      latitude == other.latitude &&
      longitude == other.longitude &&
      altitude == other.altitude &&
      position_covariance == other.position_covariance &&
      position_covariance_type == other.position_covariance_type;
  }
  bool operator!=(const NavSatFix & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_NAVSATFIX_HPP_
