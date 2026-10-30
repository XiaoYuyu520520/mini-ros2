#ifndef GEOMETRY_MSGS_MSG_QUATERNION_HPP_
#define GEOMETRY_MSGS_MSG_QUATERNION_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Quaternion
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Quaternion";
  using SharedPtr = std::shared_ptr<Quaternion>;
  using ConstSharedPtr = std::shared_ptr<const Quaternion>;
  using UniquePtr = std::unique_ptr<Quaternion>;
  using _x_type = double;
  double x = 0.0;
  using _y_type = double;
  double y = 0.0;
  using _z_type = double;
  double z = 0.0;
  using _w_type = double;
  double w = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(x);
    ar(y);
    ar(z);
    ar(w);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(x);
    ar(y);
    ar(z);
    ar(w);
  }

  bool operator==(const Quaternion & other) const
  {
    return x == other.x &&
      y == other.y &&
      z == other.z &&
      w == other.w;
  }
  bool operator!=(const Quaternion & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_QUATERNION_HPP_
