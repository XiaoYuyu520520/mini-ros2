#ifndef GEOGRAPHIC_MSGS_MSG_WAYPOINT_HPP_
#define GEOGRAPHIC_MSGS_MSG_WAYPOINT_HPP_

#include "geographic_msgs/msg/geo_point.hpp"
#include "geographic_msgs/msg/key_value.hpp"
#include "unique_identifier_msgs/msg/uuid.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace geographic_msgs
{
namespace msg
{

struct WayPoint
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geographic_msgs/msg/WayPoint";
  using SharedPtr = std::shared_ptr<WayPoint>;
  using ConstSharedPtr = std::shared_ptr<const WayPoint>;
  using UniquePtr = std::unique_ptr<WayPoint>;
  using _id_type = ::unique_identifier_msgs::msg::UUID;
  ::unique_identifier_msgs::msg::UUID id;
  using _position_type = ::geographic_msgs::msg::GeoPoint;
  ::geographic_msgs::msg::GeoPoint position;
  using _props_type = std::vector<::geographic_msgs::msg::KeyValue>;
  std::vector<::geographic_msgs::msg::KeyValue> props;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(id);
    ar(position);
    ar(props);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(id);
    ar(position);
    ar(props);
  }

  bool operator==(const WayPoint & other) const
  {
    return id == other.id &&
      position == other.position &&
      props == other.props;
  }
  bool operator!=(const WayPoint & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geographic_msgs

#endif  // GEOGRAPHIC_MSGS_MSG_WAYPOINT_HPP_
