#ifndef VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERCONTROL_HPP_
#define VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERCONTROL_HPP_

#include "geometry_msgs/msg/quaternion.hpp"
#include <vector>
#include "visualization_msgs/msg/marker.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct InteractiveMarkerControl
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/InteractiveMarkerControl";
  using SharedPtr = std::shared_ptr<InteractiveMarkerControl>;
  using ConstSharedPtr = std::shared_ptr<const InteractiveMarkerControl>;
  using UniquePtr = std::unique_ptr<InteractiveMarkerControl>;
  static constexpr uint8_t INHERIT = 0;
  static constexpr uint8_t FIXED = 1;
  static constexpr uint8_t VIEW_FACING = 2;
  static constexpr uint8_t NONE = 0;
  static constexpr uint8_t MENU = 1;
  static constexpr uint8_t BUTTON = 2;
  static constexpr uint8_t MOVE_AXIS = 3;
  static constexpr uint8_t MOVE_PLANE = 4;
  static constexpr uint8_t ROTATE_AXIS = 5;
  static constexpr uint8_t MOVE_ROTATE = 6;
  static constexpr uint8_t MOVE_3D = 7;
  static constexpr uint8_t ROTATE_3D = 8;
  static constexpr uint8_t MOVE_ROTATE_3D = 9;

  using _name_type = std::string;
  std::string name;
  using _orientation_type = ::geometry_msgs::msg::Quaternion;
  ::geometry_msgs::msg::Quaternion orientation;
  using _orientation_mode_type = uint8_t;
  uint8_t orientation_mode = 0;
  using _interaction_mode_type = uint8_t;
  uint8_t interaction_mode = 0;
  using _always_visible_type = bool;
  bool always_visible = false;
  using _markers_type = std::vector<::visualization_msgs::msg::Marker>;
  std::vector<::visualization_msgs::msg::Marker> markers;
  using _independent_marker_orientation_type = bool;
  bool independent_marker_orientation = false;
  using _description_type = std::string;
  std::string description;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(orientation);
    ar(orientation_mode);
    ar(interaction_mode);
    ar(always_visible);
    ar(markers);
    ar(independent_marker_orientation);
    ar(description);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(orientation);
    ar(orientation_mode);
    ar(interaction_mode);
    ar(always_visible);
    ar(markers);
    ar(independent_marker_orientation);
    ar(description);
  }

  bool operator==(const InteractiveMarkerControl & other) const
  {
    return name == other.name &&
      orientation == other.orientation &&
      orientation_mode == other.orientation_mode &&
      interaction_mode == other.interaction_mode &&
      always_visible == other.always_visible &&
      markers == other.markers &&
      independent_marker_orientation == other.independent_marker_orientation &&
      description == other.description;
  }
  bool operator!=(const InteractiveMarkerControl & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERCONTROL_HPP_
