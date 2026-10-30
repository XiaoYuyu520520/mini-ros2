#ifndef ROSGRAPH_MSGS_MSG_GRAPH_HPP_
#define ROSGRAPH_MSGS_MSG_GRAPH_HPP_

#include "rosgraph_msgs/msg/node.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct Graph
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/Graph";
  using SharedPtr = std::shared_ptr<Graph>;
  using ConstSharedPtr = std::shared_ptr<const Graph>;
  using UniquePtr = std::unique_ptr<Graph>;
  using _nodes_type = std::vector<::rosgraph_msgs::msg::Node>;
  std::vector<::rosgraph_msgs::msg::Node> nodes;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(nodes);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(nodes);
  }

  bool operator==(const Graph & other) const
  {
    return nodes == other.nodes;
  }
  bool operator!=(const Graph & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_GRAPH_HPP_
