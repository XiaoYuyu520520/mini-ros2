#ifndef NAV2_MSGS_MSG_ROUTE_HPP_
#define NAV2_MSGS_MSG_ROUTE_HPP_

#include "nav2_msgs/msg/route_edge.hpp"
#include "nav2_msgs/msg/route_node.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav2_msgs
{
namespace msg
{

struct Route
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav2_msgs/msg/Route";
  using SharedPtr = std::shared_ptr<Route>;
  using ConstSharedPtr = std::shared_ptr<const Route>;
  using UniquePtr = std::unique_ptr<Route>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _route_cost_type = float;
  float route_cost = 0.0f;
  using _nodes_type = std::vector<::nav2_msgs::msg::RouteNode>;
  std::vector<::nav2_msgs::msg::RouteNode> nodes;
  using _edges_type = std::vector<::nav2_msgs::msg::RouteEdge>;
  std::vector<::nav2_msgs::msg::RouteEdge> edges;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(route_cost);
    ar(nodes);
    ar(edges);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(route_cost);
    ar(nodes);
    ar(edges);
  }

  bool operator==(const Route & other) const
  {
    return header == other.header &&
      route_cost == other.route_cost &&
      nodes == other.nodes &&
      edges == other.edges;
  }
  bool operator!=(const Route & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav2_msgs

#endif  // NAV2_MSGS_MSG_ROUTE_HPP_
