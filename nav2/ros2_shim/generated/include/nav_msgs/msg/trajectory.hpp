#ifndef NAV_MSGS_MSG_TRAJECTORY_HPP_
#define NAV_MSGS_MSG_TRAJECTORY_HPP_

#include "nav_msgs/msg/trajectory_point.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct Trajectory
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/Trajectory";
  using SharedPtr = std::shared_ptr<Trajectory>;
  using ConstSharedPtr = std::shared_ptr<const Trajectory>;
  using UniquePtr = std::unique_ptr<Trajectory>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _points_type = std::vector<::nav_msgs::msg::TrajectoryPoint>;
  std::vector<::nav_msgs::msg::TrajectoryPoint> points;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(points);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(points);
  }

  bool operator==(const Trajectory & other) const
  {
    return header == other.header &&
      points == other.points;
  }
  bool operator!=(const Trajectory & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_TRAJECTORY_HPP_
