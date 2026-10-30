// `-r FROM:=TO`: which name a node ends up on.
//
// A remap is the one piece of ROS 2 configuration that is invisible from the
// outside: a rule that is ignored leaves a node on the name its source spells,
// and nothing fails - the velocity smoother listens to `/cmd_vel` while the
// controller publishes `/cmd_vel_nav`, both topics exist, both have traffic, and
// the robot simply never moves. That is the failure this file exists to make
// impossible to reintroduce.
//
//   1. with no rules, name resolution is what it always was: absolute names stay,
//      relative names get the namespace, `~/x` is the node's own name,
//   2. a rule matches a name however the source spelled it - `cmd_vel`,
//      `/cmd_vel` and either spelling of the target all land on the same name,
//   3. the first rule that matches wins, like rcl's list walk,
//   4. rules resolve against the node's namespace, on both sides,
//   5. `__node` / `__ns` still do their own job, including a rule that carries a
//      node filter (`global_costmap:__ns:=global_costmap`), because the filter
//      names the node being built,
//   6. a rule reaches the *channel*, not just the string: a subscription created
//      on the remapped name is counted under the name it was remapped to,
//   7. and it applies to services too, which go through the same call.
//
// The port's own use of this is `nav2_stack.bat --safety`, which is what puts
// `velocity_smoother` and `collision_monitor` between the controller and the
// wheels (see bridge/docs/02-running.md).
#include "rclcpp/node.hpp"
#include "rclcpp/node_options.hpp"

#include "ros2_shim/graph.hpp"

#include "lifecycle_msgs/srv/change_state.hpp"
#include "std_msgs/msg/string.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const std::string & what)
{
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++failures;
  }
}

// Compares the resolved name, and names the failing case in the message so a
// regression says which rule broke rather than only that one did.
void expectName(const rclcpp::Node & node, const std::string & asked, const std::string & want)
{
  const std::string got = node.resolve_topic(asked);
  if (got != want) {
    std::printf("  FAIL %s -> %s (wanted %s)\n", asked.c_str(), got.c_str(), want.c_str());
    ++failures;
  }
}

// `-r` and `--remap` are the same argument, and both were taken by the runners'
// command line; the shorter spelling is what the bat files use.
rclcpp::NodeOptions optionsWith(const std::vector<std::string> & rules, bool long_form = false)
{
  std::vector<std::string> arguments{"--ros-args"};
  for (const std::string & rule : rules) {
    arguments.push_back(long_form ? "--remap" : "-r");
    arguments.push_back(rule);
  }
  rclcpp::NodeOptions options;
  options.arguments(arguments);
  return options;
}

std::string resolved(const rclcpp::Node & node, const std::string & name)
{
  return node.resolve_topic(name);
}

}  // namespace

int main()
{
  std::printf("remap: name resolution under -r\n");

  // 1. No rules at all: the behaviour every other test and binary relies on.
  {
    rclcpp::Node node("talker", optionsWith({}));
    expectName(node, "/abs", "/abs");
    expectName(node, "rel", "/rel");
    expectName(node, "~/priv", "/talker/priv");
    expect(node.remap_rules().empty(), "no rules parsed");
  }

  // 2. The spelling of the rule does not matter, the name it resolves to does.
  {
    rclcpp::Node node("talker", optionsWith({"cmd_vel:=cmd_vel_nav"}));
    expectName(node, "cmd_vel", "/cmd_vel_nav");
    expectName(node, "/cmd_vel", "/cmd_vel_nav");
    expectName(node, "cmd_vel_nav", "/cmd_vel_nav");
    expectName(node, "/other", "/other");
    expectName(node, "other", "/other");
    expect(node.remap_rules().size() == 1, "one rule parsed");
  }
  {
    rclcpp::Node node("talker", optionsWith({"/cmd_vel:=/cmd_vel_nav"}));
    expectName(node, "cmd_vel", "/cmd_vel_nav");
    expectName(node, "/cmd_vel", "/cmd_vel_nav");
  }
  {
    rclcpp::Node node("talker", optionsWith({"cmd_vel:=cmd_vel_nav"}, true));
    expectName(node, "cmd_vel", "/cmd_vel_nav");
  }

  // 3. First match wins, exactly like rcl walking the list.
  {
    rclcpp::Node node("talker", optionsWith({"a:=b", "a:=c"}));
    expectName(node, "a", "/b");
  }

  // 4. Both sides resolve against the node's namespace.
  {
    rclcpp::Node node("talker", "/robot", optionsWith({"vel:=vel_out"}));
    expectName(node, "vel", "/robot/vel_out");
    expectName(node, "/vel", "/vel");
  }
  {
    // A rule whose source is absolute only ever matches an absolute name: the
    // node in `/robot` that asks for `vel` resolves to `/robot/vel`, which is
    // not the `/vel` the rule names. That is rcl's reading too, and it is why
    // the bat files write their rules without a leading slash.
    rclcpp::Node node("talker", "/robot", optionsWith({"/vel:=/vel_out"}));
    expectName(node, "/vel", "/vel_out");
    expectName(node, "vel", "/robot/vel");
    expectName(node, "/other", "/other");
  }

  // 5. `__node` / `__ns` keep their own meaning, with and without a filter.
  {
    rclcpp::Node node("talker", optionsWith({"__node:=renamed", "__ns:=/robot"}));
    expect(std::string(node.get_name()) == "renamed", "  -r __node:= renames the node");
    expect(std::string(node.get_namespace()) == "/robot", "  -r __ns:= moves the node");
    expect(node.remap_rules().empty(), "the two special keys are not topic rules");
    expectName(node, "priv", "/robot/priv");
  }
  {
    rclcpp::Node node(
      "costmap", optionsWith(
        {"global_costmap:__ns:=global_costmap", "global_costmap:__node:=global_costmap",
          "cmd_vel:=cmd_vel_nav"}));
    expect(std::string(node.get_namespace()) == "/global_costmap",
      "  a node-filtered __ns still applies");
    // Both sides of a relative rule expand against the namespace the node now
    // has, so the rule lands inside it - the controller's costmap and the
    // controller itself are different nodes with different namespaces, and a
    // rule has to mean the same node on both ends.
    expectName(node, "cmd_vel", "/global_costmap/cmd_vel_nav");
  }

  // 6. The rule reaches the channel: the graph counts the subscription under the
  //    name it was remapped to, because that is the channel it opened.
  {
    rclcpp::Node node("talker", optionsWith({"chatter:=talk"}));
    auto subscription = node.create_subscription<std_msgs::msg::String>(
      "chatter", rclcpp::QoS(1), [](const std_msgs::msg::String &) {});
    // `count_subscribers("chatter")` goes through the same remap as the
    // subscription did, so it is the *raw* graph that shows which name the
    // channel is really on.
    expect(node.count_subscribers("chatter") == 1, "the node sees one subscriber under either name");
    expect(ros2_shim::graph::subscription_count("/talk") == 1, "the channel is /talk");
    expect(ros2_shim::graph::subscription_count("/chatter") == 0, "and /chatter has none");
    (void)subscription;
  }

  // 7. Services resolve through the same call - which is what lets a remapped
  //    `/<node>/change_state` be where the lifecycle manager finds a node.
  {
    rclcpp::Node node("talker", optionsWith({"change_state:=state_service"}));
    auto service = node.create_service<lifecycle_msgs::srv::ChangeState>(
      "change_state",
      [](std::shared_ptr<lifecycle_msgs::srv::ChangeState::Request>,
      std::shared_ptr<lifecycle_msgs::srv::ChangeState::Response>) {});
    expect(resolved(node, "change_state") == "/state_service", "the service name is remapped");
    (void)service;
  }

  if (failures != 0) {
    std::printf("remap: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("remap: all checks passed\n");
  return 0;
}
