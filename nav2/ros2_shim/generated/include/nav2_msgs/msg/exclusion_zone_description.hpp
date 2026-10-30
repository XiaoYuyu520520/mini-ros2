#ifndef NAV2_MSGS_MSG_EXCLUSIONZONEDESCRIPTION_HPP_
#define NAV2_MSGS_MSG_EXCLUSIONZONEDESCRIPTION_HPP_

#include "geometry_msgs/msg/point32.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct ExclusionZoneDescription
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/ExclusionZoneDescription";
  using SharedPtr = std::shared_ptr<ExclusionZoneDescription>;
  using ConstSharedPtr = std::shared_ptr<const ExclusionZoneDescription>;
  using UniquePtr = std::unique_ptr<ExclusionZoneDescription>;
  using _zone_name_type = std::string;
  std::string zone_name;
  using _type_type = std::string;
  std::string type;
  using _frame_id_type = std::string;
  std::string frame_id;
  using _points_type = std::vector<::geometry_msgs::msg::Point32>;
  std::vector<::geometry_msgs::msg::Point32> points;
  using _radius_type = double;
  double radius = 0.0;
  using _min_height_type = double;
  double min_height = 0.0;
  using _max_height_type = double;
  double max_height = 0.0;
  using _enabled_type = bool;
  bool enabled = false;
  using _visualize_type = bool;
  bool visualize = false;
  using _frame_hold_timeout_type = double;
  double frame_hold_timeout = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(zone_name);
    ar(type);
    ar(frame_id);
    ar(points);
    ar(radius);
    ar(min_height);
    ar(max_height);
    ar(enabled);
    ar(visualize);
    ar(frame_hold_timeout);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(zone_name);
    ar(type);
    ar(frame_id);
    ar(points);
    ar(radius);
    ar(min_height);
    ar(max_height);
    ar(enabled);
    ar(visualize);
    ar(frame_hold_timeout);
  }

  bool operator==(const ExclusionZoneDescription & other) const
  {
    return zone_name == other.zone_name &&
      type == other.type &&
      frame_id == other.frame_id &&
      points == other.points &&
      radius == other.radius &&
      min_height == other.min_height &&
      max_height == other.max_height &&
      enabled == other.enabled &&
      visualize == other.visualize &&
      frame_hold_timeout == other.frame_hold_timeout;
  }
  bool operator!=(const ExclusionZoneDescription & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_EXCLUSIONZONEDESCRIPTION_HPP_
