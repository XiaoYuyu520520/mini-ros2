#ifndef NAV_MSGS_MSG_GOALS_HPP_
#define NAV_MSGS_MSG_GOALS_HPP_

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct Goals
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/Goals";
  using SharedPtr = std::shared_ptr<Goals>;
  using ConstSharedPtr = std::shared_ptr<const Goals>;
  using UniquePtr = std::unique_ptr<Goals>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _goals_type = std::vector<::geometry_msgs::msg::PoseStamped>;
  std::vector<::geometry_msgs::msg::PoseStamped> goals;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(goals);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(goals);
  }

  bool operator==(const Goals & other) const
  {
    return header == other.header &&
      goals == other.goals;
  }
  bool operator!=(const Goals & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_GOALS_HPP_
