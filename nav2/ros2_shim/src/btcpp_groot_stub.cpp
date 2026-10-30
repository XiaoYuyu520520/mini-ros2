// Groot2 status publisher, stubbed.
//
// nav2_behavior_tree's engine always names `BT::Groot2Publisher` in
// addGrootMonitoring(), even when the tree is built with Groot monitoring off
// (`enable_groot_monitoring: false` in nav2_params.yaml, which is what the port
// runs with). The class itself is fine to include - it is PIMPL'd - but its
// constructor and destructor live in groot2_publisher.cpp, which upstream only
// compiles when BTCPP_GROOT_INTERFACE is on:
//
//     if(BTCPP_GROOT_INTERFACE)
//       list(APPEND BT_SOURCE src/loggers/groot2_publisher.cpp)
//
// That interface is ZeroMQ: enabling it would put a libzmq DLL next to every
// nav2 binary, which is exactly what this port links statically to avoid. So the
// vendored BT.CPP is built without it, and the two symbols the engine still
// references get their definition here instead - a publisher that publishes
// nothing, which is what "monitoring disabled" means anyway.
#include "behaviortree_cpp/loggers/groot2_publisher.h"

#include <chrono>

namespace BT
{

// Upstream's implementation of this class keeps its ZeroMQ sockets, hooks and
// status buffer in here. Nothing has to be stored, but the type has to be
// complete: the constructor has to be able to destroy the `unique_ptr<PImpl>`
// member if anything below it throws, so an incomplete type is not enough.
struct Groot2Publisher::PImpl
{
};

Groot2Publisher::Groot2Publisher(const Tree & tree, unsigned /*server_port*/)
: StatusChangeLogger(tree.rootNode())
{
}

Groot2Publisher::~Groot2Publisher()
{
  // The base class has to stop delivering status changes before the members of
  // this object are gone; upstream's implementation does the same before it
  // tears its ZeroMQ sockets down.
  unsubscribeFromTreeChanges();
}

void Groot2Publisher::setMaxHeartbeatDelay(std::chrono::milliseconds) {}

std::chrono::milliseconds Groot2Publisher::maxHeartbeatDelay() const
{
  return std::chrono::milliseconds(0);
}

void Groot2Publisher::callback(Duration, const TreeNode &, NodeStatus, NodeStatus) {}

void Groot2Publisher::flush() {}

}  // namespace BT
