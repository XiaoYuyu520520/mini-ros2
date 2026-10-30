#ifndef ROS2_SHIM__BT_NODE_REGISTRY_HPP_
#define ROS2_SHIM__BT_NODE_REGISTRY_HPP_

#include <behaviortree_cpp/bt_factory.h>

namespace nav2_shim
{
namespace bt_registry
{

/// Registers every nav2 behavior-tree node.
///
/// Upstream loads the nodes from a plugin library per node, which calls
/// `BT_RegisterNodesFromPlugin()`. That function is `static` unless
/// `BT_PLUGIN_EXPORT` is defined, so in a statically linked binary nothing can
/// call it and the node names never reach the factory - a tree that names one
/// then fails with `Node not recognized: <name>`.
///
/// The port generates one registration wrapper per node plus the
/// `registerNodes()` implementation below them (see the nav2_behavior_tree
/// section of ros2_shim/CMakeLists.txt), and `BehaviorTreeEngine` calls this
/// from its constructor.
void registerNodes(BT::BehaviorTreeFactory & factory);

}  // namespace bt_registry
}  // namespace nav2_shim

#endif  // ROS2_SHIM__BT_NODE_REGISTRY_HPP_
