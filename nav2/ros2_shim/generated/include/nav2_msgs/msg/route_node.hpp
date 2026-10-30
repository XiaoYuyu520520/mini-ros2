#ifndef NAV2_MSGS_MSG_ROUTENODE_HPP_
#define NAV2_MSGS_MSG_ROUTENODE_HPP_

#include "geometry_msgs/msg/point.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct RouteNode
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/RouteNode";
  using SharedPtr = std::shared_ptr<RouteNode>;
  using ConstSharedPtr = std::shared_ptr<const RouteNode>;
  using UniquePtr = std::unique_ptr<RouteNode>;
  using _nodeid_type = uint16_t;
  uint16_t nodeid = 0;
  using _position_type = ::geometry_msgs::msg::Point;
  ::geometry_msgs::msg::Point position;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(nodeid);
    ar(position);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(nodeid);
    ar(position);
  }

  bool operator==(const RouteNode & other) const
  {
    return nodeid == other.nodeid &&
      position == other.position;
  }
  bool operator!=(const RouteNode & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_ROUTENODE_HPP_
