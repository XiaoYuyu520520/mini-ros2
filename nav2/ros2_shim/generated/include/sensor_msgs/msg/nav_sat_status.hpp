#ifndef SENSOR_MSGS_MSG_NAVSATSTATUS_HPP_
#define SENSOR_MSGS_MSG_NAVSATSTATUS_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct NavSatStatus
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/NavSatStatus";
  using SharedPtr = std::shared_ptr<NavSatStatus>;
  using ConstSharedPtr = std::shared_ptr<const NavSatStatus>;
  using UniquePtr = std::unique_ptr<NavSatStatus>;
  static constexpr int8_t STATUS_UNKNOWN = -2;
  static constexpr int8_t STATUS_NO_FIX = -1;
  static constexpr int8_t STATUS_FIX = 0;
  static constexpr int8_t STATUS_SBAS_FIX = 1;
  static constexpr int8_t STATUS_GBAS_FIX = 2;
  static constexpr uint16_t SERVICE_UNKNOWN = 0;
  static constexpr uint16_t SERVICE_GPS = 1;
  static constexpr uint16_t SERVICE_GLONASS = 2;
  static constexpr uint16_t SERVICE_COMPASS = 4;
  static constexpr uint16_t SERVICE_GALILEO = 8;

  using _status_type = int8_t;
  int8_t status = 0;
  using _service_type = uint16_t;
  uint16_t service = 0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(status);
    ar(service);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(status);
    ar(service);
  }

  bool operator==(const NavSatStatus & other) const
  {
    return status == other.status &&
      service == other.service;
  }
  bool operator!=(const NavSatStatus & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_NAVSATSTATUS_HPP_
