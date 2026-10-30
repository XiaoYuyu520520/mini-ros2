#ifndef GEOMETRY_MSGS_MSG_POINT_HPP_
#define GEOMETRY_MSGS_MSG_POINT_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Point
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Point";
  using SharedPtr = std::shared_ptr<Point>;
  using ConstSharedPtr = std::shared_ptr<const Point>;
  using UniquePtr = std::unique_ptr<Point>;
  using _x_type = double;
  double x = 0.0;
  using _y_type = double;
  double y = 0.0;
  using _z_type = double;
  double z = 0.0;

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

  bool operator==(const Point & other) const
  {
    return x == other.x &&
      y == other.y &&
      z == other.z;
  }
  bool operator!=(const Point & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POINT_HPP_
