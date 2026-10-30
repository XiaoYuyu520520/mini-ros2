#ifndef VISUALIZATION_MSGS_MSG_MARKER_HPP_
#define VISUALIZATION_MSGS_MSG_MARKER_HPP_

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/pose.hpp"
#include "geometry_msgs/msg/vector3.hpp"
#include "sensor_msgs/msg/compressed_image.hpp"
#include "std_msgs/msg/color_rgba.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>
#include "visualization_msgs/msg/mesh_file.hpp"
#include "visualization_msgs/msg/uv_coordinate.hpp"

#include <cstdint>
#include <memory>
#include <string>

// These constants share their names with macros a platform header defines
// (`ERROR` is one in `windows.h`), and a declaration is rewritten by the macro
// rather than compiled when one is in scope. Dropped here, once, instead of in
// every translation unit that includes both.
#ifdef DELETE
#  undef DELETE
#endif

namespace visualization_msgs
{
namespace msg
{

struct Marker
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "visualization_msgs/msg/Marker";
  using SharedPtr = std::shared_ptr<Marker>;
  using ConstSharedPtr = std::shared_ptr<const Marker>;
  using UniquePtr = std::unique_ptr<Marker>;
  static constexpr int32_t ARROW = 0;
  static constexpr int32_t CUBE = 1;
  static constexpr int32_t SPHERE = 2;
  static constexpr int32_t CYLINDER = 3;
  static constexpr int32_t LINE_STRIP = 4;
  static constexpr int32_t LINE_LIST = 5;
  static constexpr int32_t CUBE_LIST = 6;
  static constexpr int32_t SPHERE_LIST = 7;
  static constexpr int32_t POINTS = 8;
  static constexpr int32_t TEXT_VIEW_FACING = 9;
  static constexpr int32_t MESH_RESOURCE = 10;
  static constexpr int32_t TRIANGLE_LIST = 11;
  static constexpr int32_t ARROW_STRIP = 12;
  static constexpr int32_t ADD = 0;
  static constexpr int32_t MODIFY = 0;
  static constexpr int32_t DELETE = 2;
  static constexpr int32_t DELETEALL = 3;

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
  using _pose_type = ::geometry_msgs::msg::Pose;
  ::geometry_msgs::msg::Pose pose;
  using _scale_type = ::geometry_msgs::msg::Vector3;
  ::geometry_msgs::msg::Vector3 scale;
  using _color_type = ::std_msgs::msg::ColorRGBA;
  ::std_msgs::msg::ColorRGBA color;
  using _lifetime_type = ::builtin_interfaces::msg::Duration;
  ::builtin_interfaces::msg::Duration lifetime;
  using _frame_locked_type = bool;
  bool frame_locked = false;
  using _points_type = std::vector<::geometry_msgs::msg::Point>;
  std::vector<::geometry_msgs::msg::Point> points;
  using _colors_type = std::vector<::std_msgs::msg::ColorRGBA>;
  std::vector<::std_msgs::msg::ColorRGBA> colors;
  using _texture_resource_type = std::string;
  std::string texture_resource;
  using _texture_type = ::sensor_msgs::msg::CompressedImage;
  ::sensor_msgs::msg::CompressedImage texture;
  using _uv_coordinates_type = std::vector<::visualization_msgs::msg::UVCoordinate>;
  std::vector<::visualization_msgs::msg::UVCoordinate> uv_coordinates;
  using _text_type = std::string;
  std::string text;
  using _mesh_resource_type = std::string;
  std::string mesh_resource;
  using _mesh_file_type = ::visualization_msgs::msg::MeshFile;
  ::visualization_msgs::msg::MeshFile mesh_file;
  using _mesh_use_embedded_materials_type = bool;
  bool mesh_use_embedded_materials = false;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(ns);
    ar(id);
    ar(type);
    ar(action);
    ar(pose);
    ar(scale);
    ar(color);
    ar(lifetime);
    ar(frame_locked);
    ar(points);
    ar(colors);
    ar(texture_resource);
    ar(texture);
    ar(uv_coordinates);
    ar(text);
    ar(mesh_resource);
    ar(mesh_file);
    ar(mesh_use_embedded_materials);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(ns);
    ar(id);
    ar(type);
    ar(action);
    ar(pose);
    ar(scale);
    ar(color);
    ar(lifetime);
    ar(frame_locked);
    ar(points);
    ar(colors);
    ar(texture_resource);
    ar(texture);
    ar(uv_coordinates);
    ar(text);
    ar(mesh_resource);
    ar(mesh_file);
    ar(mesh_use_embedded_materials);
  }

  bool operator==(const Marker & other) const
  {
    return header == other.header &&
      ns == other.ns &&
      id == other.id &&
      type == other.type &&
      action == other.action &&
      pose == other.pose &&
      scale == other.scale &&
      color == other.color &&
      lifetime == other.lifetime &&
      frame_locked == other.frame_locked &&
      points == other.points &&
      colors == other.colors &&
      texture_resource == other.texture_resource &&
      texture == other.texture &&
      uv_coordinates == other.uv_coordinates &&
      text == other.text &&
      mesh_resource == other.mesh_resource &&
      mesh_file == other.mesh_file &&
      mesh_use_embedded_materials == other.mesh_use_embedded_materials;
  }
  bool operator!=(const Marker & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace visualization_msgs

#endif  // VISUALIZATION_MSGS_MSG_MARKER_HPP_
