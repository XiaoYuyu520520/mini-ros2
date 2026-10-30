#ifndef GEOMETRY_MSGS_MSG_POINT32_HPP_
#define GEOMETRY_MSGS_MSG_POINT32_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Point32
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Point32";
  using SharedPtr = std::shared_ptr<Point32>;
  using ConstSharedPtr = std::shared_ptr<const Point32>;
  using UniquePtr = std::unique_ptr<Point32>;
  using _x_type = float;
  float x = 0.0f;
  using _y_type = float;
  float y = 0.0f;
  using _z_type = float;
  float z = 0.0f;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(x);
    ar(y);
    ar(z);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(x);
    ar(y);
    ar(z);
  }

  bool operator==(const Point32 & other) const
  {
    return x == other.x &&
      y == other.y &&
      z == other.z;
  }
  bool operator!=(const Point32 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POINT32_HPP_
