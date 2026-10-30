#ifndef GEOGRAPHIC_MSGS_MSG_GEOPOINT_HPP_
#define GEOGRAPHIC_MSGS_MSG_GEOPOINT_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct GeoPoint
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/GeoPoint";
  using SharedPtr = std::shared_ptr<GeoPoint>;
  using ConstSharedPtr = std::shared_ptr<const GeoPoint>;
  using UniquePtr = std::unique_ptr<GeoPoint>;
  using _latitude_type = double;
  double latitude = 0.0;
  using _longitude_type = double;
  double longitude = 0.0;
  using _altitude_type = double;
  double altitude = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(latitude);
    ar(longitude);
    ar(altitude);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(latitude);
    ar(longitude);
    ar(altitude);
  }

  bool operator==(const GeoPoint & other) const
  {
    return latitude == other.latitude &&
      longitude == other.longitude &&
      altitude == other.altitude;
  }
  bool operator!=(const GeoPoint & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_GEOPOINT_HPP_
