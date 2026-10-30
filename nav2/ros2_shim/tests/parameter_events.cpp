// `/parameter_events` is the one topic a node publishes at nobody's request, and
// the only parameter interface that reaches a process which does not already
// know the node's service names. What is checked here is what goes on it:
//
//   1. declaring a parameter announces it in `new_parameters`,
//   2. an applied set announces it in `changed_parameters`, carrying the value
//      that was applied,
//   3. the message names the node that changed and carries a stamp,
//   4. a set the node refused is not announced at all,
//   5. a node built with `start_parameter_event_publisher(false)` - which is
//      every node `nav2_ros_common::generate_internal_node()` builds - writes
//      nothing and adds no publisher to the graph, while
//   6. the same flow on a node that kept the publisher does arrive, so a reader
//      that has gone deaf cannot make 5 pass, and
//   7. the topic is advertised with the type and the publisher count that
//      `ros2 topic list -t` and `ros2 topic info` read.
//
// The reader is a second node in this process: a real subscription over a real
// shared-memory ring, opened before the writer publishes, which is where a
// volatile subscriber starts. That is as far as one binary can go - that another
// process reads the same ring is what `ros2 topic echo /parameter_events` shows
// live, recorded as evidence 39 in `bridge/docs/03-validation.md`.
#include "rcl_interfaces/msg/parameter_event.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/node.hpp"
#include "ros2_shim/graph.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
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

void section(const char * name)
{
  std::printf("%s\n", name);
}

/// The names one of the message's three lists carries, so a failure prints what
/// was there instead of an index out of range.
std::string names(const std::vector<rcl_interfaces::msg::Parameter> & parameters)
{
  std::string text;
  for (const auto & parameter : parameters) {
    if (!text.empty()) {
      text += ", ";
    }
    text += parameter.name;
  }
  return text.empty() ? "(none)" : text;
}

/// A node of its own, so that the reader has a cursor of its own: two nodes in
/// one process still open two views of the channel, and a volatile subscription
/// starts at the head of the ring - it sees what is published after it exists,
/// not what an earlier writer left behind.
struct Watcher
{
  Watcher()
  : node(std::make_shared<rclcpp::Node>("parameter_events_watcher"))
  {
    subscription = node->create_subscription<rcl_interfaces::msg::ParameterEvent>(
      "/parameter_events", rclcpp::QoS(100),
      [this](const rcl_interfaces::msg::ParameterEvent & event) {events.push_back(event);});
  }

  /// Polls until `want` messages have arrived, or the budget runs out. `want`
  /// of 1 with a short budget is how "nothing was sent" is asserted.
  void drain(std::size_t want, int milliseconds = 3000)
  {
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(milliseconds);
    while (events.size() < want && std::chrono::steady_clock::now() < deadline) {
      node->spin_once();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  rclcpp::Node::SharedPtr node;
  rclcpp::Subscription<rcl_interfaces::msg::ParameterEvent>::SharedPtr subscription;
  std::vector<rcl_interfaces::msg::ParameterEvent> events;
};

/// What the graph says about the topic, which is what `ros2 topic list -t` and
/// `ros2 topic info` print. False when the topic is not advertised at all.
bool advertised(ros2_shim::graph::TopicEntry * entry)
{
  for (const auto & candidate : ros2_shim::graph::list_topics()) {
    if (candidate.name == "/parameter_events") {
      *entry = candidate;
      return true;
    }
  }
  return false;
}

int publisher_count()
{
  ros2_shim::graph::TopicEntry entry;
  return advertised(&entry) ? entry.publishers : 0;
}

void aDeclarationIsAnnouncedAsNew()
{
  section("a declared parameter is announced in new_parameters");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("talker");
  node->declare_parameter("resolution", 0.05);
  watcher.drain(1);

  expect(watcher.events.size() == 1, "one message for one declaration");
  if (watcher.events.size() != 1) {
    return;
  }
  const auto & event = watcher.events.front();
  expect(event.node == "/talker", "the message names the node: " + event.node);
  expect(event.new_parameters.size() == 1, "new_parameters: " + names(event.new_parameters));
  expect(
    event.changed_parameters.empty(), "changed_parameters: " + names(event.changed_parameters));
  expect(
    event.deleted_parameters.empty(), "deleted_parameters: " + names(event.deleted_parameters));
  if (event.new_parameters.size() == 1) {
    expect(event.new_parameters.front().name == "resolution", "the name is the one declared");
    const auto & value = event.new_parameters.front().value;
    expect(
      value.type == 3 && value.double_value == 0.05,
      "the value it was declared with, as a double");
  }
  expect(event.stamp.sec != 0 || event.stamp.nanosec != 0, "the message carries a stamp");
}

void anAppliedSetIsAnnouncedAsChanged()
{
  section("an applied set is announced in changed_parameters");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("talker");
  node->declare_parameter("resolution", 0.05);
  watcher.drain(1);
  watcher.events.clear();

  node->set_parameter(rclcpp::Parameter("resolution", 0.07));
  watcher.drain(1);

  expect(watcher.events.size() == 1, "one message for one set");
  if (watcher.events.size() != 1) {
    return;
  }
  const auto & event = watcher.events.front();
  expect(event.node == "/talker", "the message names the node: " + event.node);
  expect(
    names(event.changed_parameters) == "resolution",
    "changed_parameters: " + names(event.changed_parameters));
  expect(event.new_parameters.empty(), "new_parameters: " + names(event.new_parameters));
  if (event.changed_parameters.size() == 1) {
    const auto & value = event.changed_parameters.front().value;
    expect(
      value.type == 3 && value.double_value == 0.07,
      "the value that was applied, not the one it replaced");
  }
}

void aRefusedSetIsNotAnnounced()
{
  section("a set the node refused is not announced");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("rejecting");
  node->add_on_set_parameters_callback(
    [](const std::vector<rclcpp::Parameter> &) {
      rcl_interfaces::msg::SetParametersResult result;
      result.successful = false;
      result.reason = "refused by the test";
      return result;
    });
  node->declare_parameter("resolution", 0.05);

  // The declaration is the negative control for the silence that follows: if it
  // did not arrive, "nothing arrived" would prove nothing.
  watcher.drain(1);
  expect(watcher.events.size() == 1, "the declaration still arrives");
  watcher.events.clear();

  node->set_parameter(rclcpp::Parameter("resolution", 0.07));
  watcher.drain(1, 400);

  expect(watcher.events.empty(), "a refused set puts nothing on the topic");
  double value = -1.0;
  const bool found = node->get_parameter("resolution", value);
  expect(found && value == 0.05, "and the parameter still holds what it held");
}

void aNodeWithoutThePublisherStaysSilent()
{
  section("start_parameter_event_publisher(false) keeps a node off the topic");

  Watcher watcher;
  const int before = publisher_count();

  rclcpp::NodeOptions quiet_options;
  quiet_options.start_parameter_event_publisher(false);
  auto quiet = std::make_shared<rclcpp::Node>("quiet_node", quiet_options);
  quiet->declare_parameter("resolution", 0.05);
  quiet->set_parameter(rclcpp::Parameter("resolution", 0.07));
  watcher.drain(1, 400);

  expect(watcher.events.empty(), "an internal helper node puts nothing on the topic");
  expect(
    publisher_count() == before,
    "and advertises no publisher for it (" + std::to_string(before) + " -> " +
    std::to_string(publisher_count()) + ")");

  // The control: the same flow on a node that kept the publisher arrives, so
  // the silence above cannot be the reader having gone deaf.
  auto talker = std::make_shared<rclcpp::Node>("talker");
  talker->declare_parameter("resolution", 0.05);
  watcher.drain(1);
  expect(watcher.events.size() == 1, "the same flow on a normal node does arrive");
  expect(publisher_count() == before + 1, "and it is the one publisher that was added");
}

void theTopicIsAdvertisedWithItsType()
{
  section("the topic is advertised the way the introspection tools read it");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("graph_talker");
  (void)node;

  ros2_shim::graph::TopicEntry entry;
  expect(advertised(&entry), "/parameter_events is in the shim graph");
  if (!advertised(&entry)) {
    return;
  }
  expect(
    entry.type == "rcl_interfaces/msg/ParameterEvent", "type: " + entry.type);
  expect(
    entry.publishers >= 2,
    "every node with the publisher counts (" + std::to_string(entry.publishers) + ")");
  expect(
    entry.subscribers >= 1,
    "the watcher counts as a subscriber (" + std::to_string(entry.subscribers) + ")");
}

}  // namespace

int main()
{
  aDeclarationIsAnnouncedAsNew();
  anAppliedSetIsAnnouncedAsChanged();
  aRefusedSetIsNotAnnounced();
  aNodeWithoutThePublisherStaysSilent();
  theTopicIsAdvertisedWithItsType();

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
