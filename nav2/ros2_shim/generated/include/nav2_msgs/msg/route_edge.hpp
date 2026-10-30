#ifndef NAV2_MSGS_MSG_ROUTEEDGE_HPP_
#define NAV2_MSGS_MSG_ROUTEEDGE_HPP_

#include "geometry_msgs/msg/point.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct RouteEdge
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/RouteEdge";
  using SharedPtr = std::shared_ptr<RouteEdge>;
  using ConstSharedPtr = std::shared_ptr<const RouteEdge>;
  using UniquePtr = std::unique_ptr<RouteEdge>;
  using _edgeid_type = uint16_t;
  uint16_t edgeid = 0;
  using _start_type = ::geometry_msgs::msg::Point;
  ::geometry_msgs::msg::Point start;
  using _end_type = ::geometry_msgs::msg::Point;
  ::geometry_msgs::msg::Point end;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(edgeid);
    ar(start);
    ar(end);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(edgeid);
    ar(start);
    ar(end);
  }

  bool operator==(const RouteEdge & other) const
  {
    return edgeid == other.edgeid &&
      start == other.start &&
      end == other.end;
  }
  bool operator!=(const RouteEdge & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_ROUTEEDGE_HPP_
