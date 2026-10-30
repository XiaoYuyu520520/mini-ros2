#ifndef SHAPE_MSGS_MSG_MESH_HPP_
#define SHAPE_MSGS_MSG_MESH_HPP_

#include "geometry_msgs/msg/point.hpp"
#include "shape_msgs/msg/mesh_triangle.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace shape_msgs
{
namespace msg
{

struct Mesh
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "shape_msgs/msg/Mesh";
  using SharedPtr = std::shared_ptr<Mesh>;
  using ConstSharedPtr = std::shared_ptr<const Mesh>;
  using UniquePtr = std::unique_ptr<Mesh>;
  using _triangles_type = std::vector<::shape_msgs::msg::MeshTriangle>;
  std::vector<::shape_msgs::msg::MeshTriangle> triangles;
  using _vertices_type = std::vector<::geometry_msgs::msg::Point>;
  std::vector<::geometry_msgs::msg::Point> vertices;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(triangles);
    ar(vertices);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(triangles);
    ar(vertices);
  }

  bool operator==(const Mesh & other) const
  {
    return triangles == other.triangles &&
      vertices == other.vertices;
  }
  bool operator!=(const Mesh & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace shape_msgs

#endif  // SHAPE_MSGS_MSG_MESH_HPP_
