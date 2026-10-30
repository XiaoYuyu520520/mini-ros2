// nav2_ros_common against the shim, as a test rather than a compile check.
//
// This file used to end at `return 0`: it was written to prove that
// `nav2_ros_common/lifecycle_node.hpp` and `interface_factories.hpp` build
// against the shim, and it passed as long as nothing crashed. Registering it as
// a ctest (05-audit.md section 2.1) is only worth anything if it asserts
// something, so everything below is a contract nav2's servers actually rely on:
// the name the node carries, the QoS nav2 hands to every one of its topics, the
// two entities create_publisher()/create_subscription() build, the graph counts
// `voxel_layer` and `velocity_smoother` read to decide whether to publish at
// all, and the four transitions a lifecycle manager drives in that order.
#include "nav2_ros_common/interface_factories.hpp"
#include "nav2_ros_common/lifecycle_node.hpp"
#include "nav2_ros_common/node_thread.hpp"
#include "nav2_ros_common/simple_action_server.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/empty.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace
{

int failures = 0;

void check(bool ok, const std::string & what)
{
  if (ok) {
    std::printf("ok   %s\n", what.c_str());
  } else {
    ++failures;
    std::printf("FAIL %s\n", what.c_str());
  }
}

using State = lifecycle_msgs::msg::State;

class TestNode : public nav2::LifecycleNode
{
public:
  TestNode()
  : nav2::LifecycleNode("test_node")
  {
  }

  nav2::CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    ++configured;
    return nav2::CallbackReturn::SUCCESS;
  }

  nav2::CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    ++activated;
    return nav2::CallbackReturn::SUCCESS;
  }

  int configured = 0;
  int activated = 0;
};

}  // namespace

int main()
{
  rclcpp::init(0, nullptr);
  {
    auto node = std::make_shared<TestNode>();
    check(std::string(node->get_name()) == "test_node",
      "the node carries the name it was given");
    check(std::string(node->get_namespace()).empty(),
      "and the root namespace it was not given");

    // The shim spells the policy enumerators the RMW way (`RELIABLE`), not the
    // way rclcpp's newer headers do (`Reliable`) -- see 05-audit.md, section 7.7.
    const auto qos = nav2::qos::StandardTopicQoS();
    check(qos.get_reliability() == rclcpp::ReliabilityPolicy::RELIABLE,
      "StandardTopicQoS is reliable");
    check(qos.get_durability() == rclcpp::DurabilityPolicy::VOLATILE,
      "and volatile");
    check(qos.get_history() == rclcpp::HistoryPolicy::KEEP_LAST && qos.depth() == 10,
      "and keeps the last 10");

    // Created before configure(), the way nav2's servers create theirs.
    auto publisher = node->create_publisher<std_msgs::msg::String>("topic", qos);
    auto subscription = node->create_subscription<std_msgs::msg::String>(
      "topic", [](const std_msgs::msg::String::ConstSharedPtr) {}, qos);
    check(node->count_publishers("topic") >= 99,
      "the publisher shows up in the graph");
    check(node->count_subscribers("topic") >= 1,
      "and so does the subscription");

    check(node->get_current_state().id() == State::PRIMARY_STATE_UNCONFIGURED,
      "a fresh node starts unconfigured");

    const auto configured = node->configure();
    check(configured.id() == State::PRIMARY_STATE_INACTIVE,
      "configure() lands in inactive");
    check(configured.id() == node->get_current_state().id(),
      "and what it returns is the state the node is now in");
    check(node->configured == 1, "on_configure() ran exactly once");

    const auto activated = node->activate();
    check(activated.id() == State::PRIMARY_STATE_ACTIVE,
      "activate() lands in active");
    check(node->activated == 1, "on_activate() ran exactly once");

    const auto deactivated = node->deactivate();
    check(deactivated.id() == State::PRIMARY_STATE_INACTIVE,
      "deactivate() hands back inactive");

    const auto cleaned = node->cleanup();
    check(cleaned.id() == State::PRIMARY_STATE_UNCONFIGURED,
      "cleanup() hands back unconfigured");

    (void)publisher;
    (void)subscription;
  }
  rclcpp::shutdown();

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}