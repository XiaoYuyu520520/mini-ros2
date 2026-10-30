#ifndef VISUALIZATION_MSGS_MSG_IMAGEMARKER_HPP_
#define VISUALIZATION_MSGS_MSG_IMAGEMARKER_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "std_msgs/msg/color_rgba.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace visualization_msgs
{
namespace msg
{

struct ImageMarker
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/ImageMarker";
  using SharedPtr = std::shared_ptr<ImageMarker>;
  using ConstSharedPtr = std::shared_ptr<const ImageMarker>;
  using UniquePtr = std::unique_ptr<ImageMarker>;
  static constexpr int32_t CIRCLE = 0;
  static constexpr int32_t LINE_STRIP = 1;
  static constexpr int32_t LINE_LIST = 2;
  static constexpr int32_t POLYGON = 3;
  static constexpr int32_t POINTS = 4;
  static constexpr int32_t ADD = 0;
  static constexpr int32_t REMOVE = 1;

  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _ns_type = std::string;
  std::string ns;
  using _id_type = int32_t;
  int32_t id = 0;
  using _type_type = int32_t;
  int32_t type = 0;
  using _action_type = int32_t;
  int32_t action = 0;
  using _position_type = ::geometry_msgs::msg::Point;
  ::geometry_msgs::msg::Point position;
  using _scale_type = float;
  float scale = 0.0f;
  using _outline_color_type = ::std_msgs::msg::ColorRGBA;
  ::std_msgs::msg::ColorRGBA outline_color;
  using _filled_type = uint8_t;
  uint8_t filled = 0;
  using _fill_color_type = ::std_msgs::msg::ColorRGBA;
  ::std_msgs::msg::ColorRGBA fill_color;
  using _lifetime_type = ::builtin_interfaces::msg::Duration;
  ::builtin_interfaces::msg::Duration lifetime;
  using _points_type = std::vector<::geometry_msgs::msg::Point>;
  std::vector<::geometry_msgs::msg::Point> points;
  using _outline_colors_type = std::vector<::std_msgs::msg::ColorRGBA>;
  std::vector<::std_msgs::msg::ColorRGBA> outline_colors;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(ns);
    ar(id);
    ar(type);
    ar(action);
    ar(position);
    ar(scale);
    ar(outline_color);
    ar(filled);
    ar(fill_color);
    ar(lifetime);
    ar(points);
    ar(outline_colors);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(ns);
    ar(id);
    ar(type);
    ar(action);
    ar(position);
    ar(scale);
    ar(outline_color);
    ar(filled);
    ar(fill_color);
    ar(lifetime);
    ar(points);
    ar(outline_colors);
  }

  bool operator==(const ImageMarker & other) const
  {
    return header == other.header &&
      ns == other.ns &&
      id == other.id &&
      type == other.type &&
      action == other.action &&
      position == other.position &&
      scale == other.scale &&
      outline_color == other.outline_color &&
      filled == other.filled &&
      fill_color == other.fill_color &&
      lifetime == other.lifetime &&
      points == other.points &&
      outline_colors == other.outline_colors;
  }
  bool operator!=(const ImageMarker & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_IMAGEMARKER_HPP_
