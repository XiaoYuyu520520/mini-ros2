#ifndef SHAPE_MSGS_MSG_PLANE_HPP_
#define SHAPE_MSGS_MSG_PLANE_HPP_

#include <array>

#include <cstdint>
#include <memory>
#include <string>

namespace shape_msgs
{
namespace msg
{

struct Plane
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "shape_msgs/msg/Plane";
  using SharedPtr = std::shared_ptr<Plane>;
  using ConstSharedPtr = std::shared_ptr<const Plane>;
  using UniquePtr = std::unique_ptr<Plane>;
  using _coef_type = std::array<double, 4>;
  std::array<double, 4> coef = {};

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(coef);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(coef);
  }

  bool operator==(const Plane & other) const
  {
    return coef == other.coef;
  }
  bool operator!=(const Plane & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace shape_msgs

#endif  // SHAPE_MSGS_MSG_PLANE_HPP_
