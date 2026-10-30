#ifndef SHAPE_MSGS_MSG_MESHTRIANGLE_HPP_
#define SHAPE_MSGS_MSG_MESHTRIANGLE_HPP_

#include <array>

#include <cstdint>
#include <memory>
#include <string>

namespace shape_msgs
{
namespace msg
{

struct MeshTriangle
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "shape_msgs/msg/MeshTriangle";
  using SharedPtr = std::shared_ptr<MeshTriangle>;
  using ConstSharedPtr = std::shared_ptr<const MeshTriangle>;
  using UniquePtr = std::unique_ptr<MeshTriangle>;
  using _vertex_indices_type = std::array<uint32_t, 3>;
  std::array<uint32_t, 3> vertex_indices = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(vertex_indices);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(vertex_indices);
  }

  bool operator==(const MeshTriangle & other) const
  {
    return vertex_indices == other.vertex_indices;
  }
  bool operator!=(const MeshTriangle & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace shape_msgs

#endif  // SHAPE_MSGS_MSG_MESHTRIANGLE_HPP_
