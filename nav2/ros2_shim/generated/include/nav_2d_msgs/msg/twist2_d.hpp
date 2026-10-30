#ifndef NAV_2D_MSGS_MSG_TWIST2D_HPP_
#define NAV_2D_MSGS_MSG_TWIST2D_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace nav_2d_msgs
{
namespace msg
{

struct Twist2D
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_2d_msgs/msg/Twist2D";
  using SharedPtr = std::shared_ptr<Twist2D>;
  using ConstSharedPtr = std::shared_ptr<const Twist2D>;
  using UniquePtr = std::unique_ptr<Twist2D>;
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

  bool operator==(const Twist2D & other) const
  {
    return x == other.x &&
      y == other.y &&
      theta == other.theta;
  }
  bool operator!=(const Twist2D & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_2d_msgs

#endif  // NAV_2D_MSGS_MSG_TWIST2D_HPP_
