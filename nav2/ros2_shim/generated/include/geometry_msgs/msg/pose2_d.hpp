#ifndef GEOMETRY_MSGS_MSG_POSE2D_HPP_
#define GEOMETRY_MSGS_MSG_POSE2D_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace geometry_msgs
{
namespace msg
{

struct Pose2D
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "geometry_msgs/msg/Pose2D";
  using SharedPtr = std::shared_ptr<Pose2D>;
  using ConstSharedPtr = std::shared_ptr<const Pose2D>;
  using UniquePtr = std::unique_ptr<Pose2D>;
  using _x_type = double;
  double x = 0.0;
  using _y_type = double;
  double y = 0.0;
  using _theta_type = double;
  double theta = 0.0;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(x);
    ar(y);
    ar(theta);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(x);
    ar(y);
    ar(theta);
  }

  bool operator==(const Pose2D & other) const
  {
    return x == other.x &&
      y == other.y &&
      theta == other.theta;
  }
  bool operator!=(const Pose2D & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace geometry_msgs

#endif  // GEOMETRY_MSGS_MSG_POSE2D_HPP_
