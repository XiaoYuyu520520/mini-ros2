#ifndef NAV_2D_MSGS_MSG_POSE2D32_HPP_
#define NAV_2D_MSGS_MSG_POSE2D32_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav_2d_msgs
{
namespace msg
{

struct Pose2D32
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_2d_msgs/msg/Pose2D32";
  using SharedPtr = std::shared_ptr<Pose2D32>;
  using ConstSharedPtr = std::shared_ptr<const Pose2D32>;
  using UniquePtr = std::unique_ptr<Pose2D32>;
  using _x_type = float;
  float x = 0.0f;
  using _y_type = float;
  float y = 0.0f;
  using _theta_type = float;
  float theta = 0.0f;

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

  bool operator==(const Pose2D32 & other) const
  {
    return x == other.x &&
      y == other.y &&
      theta == other.theta;
  }
  bool operator!=(const Pose2D32 & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_2d_msgs

#endif  // NAV_2D_MSGS_MSG_POSE2D32_HPP_
