#ifndef VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERFEEDBACK_HPP_
#define VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERFEEDBACK_HPP_

#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "std_msgs/msg/header.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct InteractiveMarkerFeedback
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/InteractiveMarkerFeedback";
  using SharedPtr = std::shared_ptr<InteractiveMarkerFeedback>;
  using ConstSharedPtr = std::shared_ptr<const InteractiveMarkerFeedback>;
  using UniquePtr = std::unique_ptr<InteractiveMarkerFeedback>;
  static constexpr uint8_t KEEP_ALIVE = 0;
  static constexpr uint8_t POSE_UPDATE = 1;
  static constexpr uint8_t MENU_SELECT = 2;
  static constexpr uint8_t BUTTON_CLICK = 3;
  static constexpr uint8_t MOUSE_DOWN = 4;
  static constexpr uint8_t MOUSE_UP = 5;

  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _client_id_type = std::string;
  std::string client_id;
  using _marker_name_type = std::string;
  std::string marker_name;
  using _control_name_type = std::string;
  std::string control_name;
  using _event_type_type = uint8_t;
  uint8_t event_type = 0;
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;
  using _menu_entry_id_type = uint32_t;
  uint32_t menu_entry_id = 0;
  using _mouse_point_type = ::geometry_msgs::msg::Point;
  ::geometry_msgs::msg::Point mouse_point;
  using _mouse_point_valid_type = bool;
  bool mouse_point_valid = false;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(client_id);
    ar(marker_name);
    ar(control_name);
    ar(event_type);
    ar(pose);
    ar(menu_entry_id);
    ar(mouse_point);
    ar(mouse_point_valid);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(client_id);
    ar(marker_name);
    ar(control_name);
    ar(event_type);
    ar(pose);
    ar(menu_entry_id);
    ar(mouse_point);
    ar(mouse_point_valid);
  }

  bool operator==(const InteractiveMarkerFeedback & other) const
  {
    return header == other.header &&
      client_id == other.client_id &&
      marker_name == other.marker_name &&
      control_name == other.control_name &&
      event_type == other.event_type &&
      pose == other.pose &&
      menu_entry_id == other.menu_entry_id &&
      mouse_point == other.mouse_point &&
      mouse_point_valid == other.mouse_point_valid;
  }
  bool operator!=(const InteractiveMarkerFeedback & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_INTERACTIVEMARKERFEEDBACK_HPP_
