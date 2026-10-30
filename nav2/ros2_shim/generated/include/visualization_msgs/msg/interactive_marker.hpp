#ifndef VISUALIZATION_MSGS_MSG_INTERACTIVEMARKER_HPP_
#define VISUALIZATION_MSGS_MSG_INTERACTIVEMARKER_HPP_

#include "geometry_msgs/msg/pose.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>
#include "visualization_msgs/msg/interactive_marker_control.hpp"
#include "visualization_msgs/msg/menu_entry.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct InteractiveMarker
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/InteractiveMarker";
  using SharedPtr = std::shared_ptr<InteractiveMarker>;
  using ConstSharedPtr = std::shared_ptr<const InteractiveMarker>;
  using UniquePtr = std::unique_ptr<InteractiveMarker>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;
  using _name_type = std::string;
  std::string name;
  using _description_type = std::string;
  std::string description;
  using _scale_type = float;
  float scale = 0.0f;
  using _menu_entries_type = std::vector<::visualization_msgs::msg::MenuEntry>;
  std::vector<::visualization_msgs::msg::MenuEntry> menu_entries;
  using _controls_type = std::vector<::visualization_msgs::msg::InteractiveMarkerControl>;
  std::vector<::visualization_msgs::msg::InteractiveMarkerControl> controls;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(pose);
    ar(name);
    ar(description);
    ar(scale);
    ar(menu_entries);
    ar(controls);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(pose);
    ar(name);
    ar(description);
    ar(scale);
    ar(menu_entries);
    ar(controls);
  }

  bool operator==(const InteractiveMarker & other) const
  {
    return header == other.header &&
      pose == other.pose &&
      name == other.name &&
      description == other.description &&
      scale == other.scale &&
      menu_entries == other.menu_entries &&
      controls == other.controls;
  }
  bool operator!=(const InteractiveMarker & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_INTERACTIVEMARKER_HPP_
