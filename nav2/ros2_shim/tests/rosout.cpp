// `/rosout` is the topic every node's log lands on: what `ros2 topic echo
// /rosout` reads, and what a log aggregator subscribes to instead of scraping a
// console. Upstream rclcpp opens it on every node, so without it `ros2 topic
// list` was a topic short of a real robot's for each node in the process - and
// the shim's own `RCLCPP_*` output, which is what nav2 talks through, was
// visible to nothing but stderr.
//
// What is pinned here:
//
//   1. a node advertises `/rosout` with the type `ros2 topic list -t` prints,
//   2. a `RCLCPP_INFO` arrives carrying the text, the severity number, the
//      node's name, and the call site (`file` / `function` / `line`) that
//      upstream fills in from the macro,
//   3. every severity keeps its own number - 20/30/40/50, which is not the
//      order of the enum behind them,
//   4. the `_STREAM` macros produce the same message the printf-shaped ones do,
//   5. `enable_rosout(false)` adds no publisher to the graph *and* makes that
//      node's own log silent on the topic, with a node that kept the publisher
//      as the control,
//   6. a logger no node owns - `rclcpp::get_logger("...")`, which is how
//      library code logs - is still published, through the first node,
//   7. the publisher is gone from the graph once the node is.
//
// DEBUG is deliberately not asserted by number: `NAV2_SHM_LOG_LEVEL` gates one
// stream for both outputs, so a DEBUG line the console suppresses does not
// appear on the topic either. That is a deviation from upstream, where the two
// are filtered separately, and it is why this test would fail if it inherited
// `bridge_env.bat`'s `NAV2_SHM_LOG_LEVEL=info` - see caveat 3 in
// `bridge/docs/04-gaps.md`.
//
// The reader is a second node in this process: a real subscription over a real
// shared-memory ring, opened before the writer publishes, which is where a
// volatile subscriber starts. That another *process* reads the same ring is what
// `ros2 topic echo /rosout` shows live, recorded as evidence 50 in
// `bridge/docs/03-validation.md`.
#include "rcl_interfaces/msg/log.hpp"
#include "rclcpp/logging.hpp"
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

std::string describe(const rcl_interfaces::msg::Log & message)
{
  return "level " + std::to_string(message.level) + " name " + message.name +
         " msg \"" + message.msg + "\"";
}

/// A node of its own, so the reader has a cursor of its own: a volatile
/// subscription starts at the head of the ring and sees what is published after
/// it exists, not what an earlier writer left behind.
struct Watcher
{
  Watcher()
  : node(std::make_shared<rclcpp::Node>("rosout_watcher"))
  {
    subscription = node->create_subscription<rcl_interfaces::msg::Log>(
      "/rosout", rclcpp::QoS(100),
      [this](const rcl_interfaces::msg::Log & message) {messages.push_back(message);});
  }

  /// Polls until `want` messages have arrived, or the budget runs out. `want` of
  /// 1 with a short budget is how "nothing was sent" is asserted.
  void drain(std::size_t want, int milliseconds = 3000)
  {
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(milliseconds);
    while (messages.size() < want && std::chrono::steady_clock::now() < deadline) {
      node->spin_once();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  rclcpp::Node::SharedPtr node;
  rclcpp::Subscription<rcl_interfaces::msg::Log>::SharedPtr subscription;
  std::vector<rcl_interfaces::msg::Log> messages;
};

/// What the graph says about the topic, which is what `ros2 topic list -t` and
/// `ros2 topic info` print. False when the topic is not advertised at all.
bool advertised(ros2_shim::graph::TopicEntry * entry)
{
  for (const auto & candidate : ros2_shim::graph::list_topics()) {
    if (candidate.name == "/rosout") {
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

void anInfoLineArrivesWithItsLevelAndNameAndCallSite()
{
  section("a log line arrives with its text, level, name and call site");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("talker");
  RCLCPP_INFO(node->get_logger(), "planning %d waypoints", 7);
  watcher.drain(1);

  expect(
    watcher.messages.size() == 1,
    "one message for one line: " + std::to_string(watcher.messages.size()));
  if (watcher.messages.size() != 1) {
    return;
  }
  const auto & message = watcher.messages.front();
  expect(message.level == 20, "INFO is 20: " + std::to_string(message.level));
  expect(message.name == "talker", "the name is the node's: " + message.name);
  expect(message.msg == "planning 7 waypoints", "the formatted text: " + message.msg);
  expect(
    message.stamp.sec != 0 || message.stamp.nanosec != 0, "the message carries a stamp");
  expect(
    message.file.find("rosout.cpp") != std::string::npos, "file: " + message.file);
  expect(
    message.function == "anInfoLineArrivesWithItsLevelAndNameAndCallSite",
    "function: " + message.function);
  expect(message.line != 0, "line: " + std::to_string(message.line));

  // One line is one message. A second line adds exactly one more, which is what
  // would fail if the message were handed to every sink in the process.
  RCLCPP_INFO(node->get_logger(), "second line");
  watcher.drain(2, 500);
  expect(
    watcher.messages.size() == 2,
    "exactly one message per line: " + std::to_string(watcher.messages.size()));
}

void everySeverityKeepsItsNumber()
{
  section("each severity arrives with the number the message type carries");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("severities");
  RCLCPP_WARN(node->get_logger(), "warn line");
  RCLCPP_ERROR(node->get_logger(), "error line");
  RCLCPP_FATAL(node->get_logger(), "fatal line");
  watcher.drain(3);

  expect(
    watcher.messages.size() == 3,
    "three lines, three messages: " + std::to_string(watcher.messages.size()));
  if (watcher.messages.size() != 3) {
    return;
  }
  expect(watcher.messages[0].level == 30, "warn: " + describe(watcher.messages[0]));
  expect(watcher.messages[1].level == 40, "error: " + describe(watcher.messages[1]));
  expect(watcher.messages[2].level == 50, "fatal: " + describe(watcher.messages[2]));
  expect(
    watcher.messages[1].msg == "error line",
    "and the text goes with it: " + watcher.messages[1].msg);
}

void aStreamMacroProducesTheSameMessage()
{
  section("the _STREAM macros produce the same message");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("streamer");
  RCLCPP_WARN_STREAM(node->get_logger(), "costmap " << 3 << " layers");
  watcher.drain(1);

  expect(
    watcher.messages.size() == 1,
    "one message for one stream line: " + std::to_string(watcher.messages.size()));
  if (watcher.messages.size() != 1) {
    return;
  }
  const auto & message = watcher.messages.front();
  expect(message.msg == "costmap 3 layers", "the streamed text: " + message.msg);
  expect(message.level == 30, "warn: " + describe(message));
  expect(message.name == "streamer", "the name is the node's: " + message.name);
}

void aNodeThatDeclinesTheTopicIsSilent()
{
  section("enable_rosout(false) opens no publisher and silences that node");

  Watcher watcher;
  const int before = publisher_count();

  rclcpp::NodeOptions quiet_options;
  quiet_options.enable_rosout(false);
  auto quiet = std::make_shared<rclcpp::Node>("quiet_node", quiet_options);

  expect(
    publisher_count() == before,
    "a node that declined adds no publisher (" + std::to_string(before) + " -> " +
    std::to_string(publisher_count()) + ")");

  RCLCPP_ERROR(quiet->get_logger(), "must not reach the topic");
  watcher.drain(1, 400);

  expect(
    watcher.messages.empty(),
    "and its own log stays off the topic" +
    (watcher.messages.empty() ? std::string() : ": " + describe(watcher.messages.front())));

  // The control: a node that kept the publisher is heard, so the silence above
  // cannot be the reader having gone deaf.
  auto heard = std::make_shared<rclcpp::Node>("heard_node");
  RCLCPP_ERROR(heard->get_logger(), "this one arrives");
  watcher.drain(1);

  expect(watcher.messages.size() == 1, "the same call on a normal node does arrive");
  if (watcher.messages.size() == 1) {
    expect(
      watcher.messages.front().msg == "this one arrives",
      "with its text: " + watcher.messages.front().msg);
  }
  expect(
    publisher_count() == before + 1,
    "and it is the one publisher that was added (" + std::to_string(before) + " -> " +
    std::to_string(publisher_count()) + ")");
}

void aLoggerNoNodeOwnsIsStillPublished()
{
  section("a logger no node owns goes out through the first node's publisher");

  Watcher watcher;
  RCLCPP_INFO(rclcpp::get_logger("standalone_probe"), "from library code");
  watcher.drain(1);

  expect(
    watcher.messages.size() == 1,
    "the message arrives rather than being dropped: " +
    std::to_string(watcher.messages.size()));
  if (watcher.messages.size() != 1) {
    return;
  }
  const auto & message = watcher.messages.front();
  expect(message.msg == "from library code", "the text: " + message.msg);
  expect(
    message.name == "standalone_probe",
    "and the name is the logger's, not the publishing node's: " + message.name);
}

void theTopicIsAdvertisedWithItsType()
{
  section("the topic is advertised the way the introspection tools read it");

  Watcher watcher;
  auto node = std::make_shared<rclcpp::Node>("graph_talker");
  (void)node;

  ros2_shim::graph::TopicEntry entry;
  expect(advertised(&entry), "/rosout is in the shim graph");
  if (!advertised(&entry)) {
    return;
  }
  expect(entry.type == "rcl_interfaces/msg/Log", "type: " + entry.type);
  expect(
    entry.publishers >= 2,
    "every node that took one counts (" + std::to_string(entry.publishers) + ")");
  expect(
    entry.subscribers >= 1,
    "the watcher counts as a subscriber (" + std::to_string(entry.subscribers) + ")");
}

void thePublisherIsGivenBackWhenTheNodeDies()
{
  section("the graph loses the publisher when the node does");

  const int before = publisher_count();
  {
    auto node = std::make_shared<rclcpp::Node>("transient_node");
    expect(
      publisher_count() == before + 1,
      "a node adds one publisher (" + std::to_string(before) + " -> " +
      std::to_string(publisher_count()) + ")");
  }
  expect(
    publisher_count() == before,
    "and gives it back (" + std::to_string(before + 1) + " -> " +
    std::to_string(publisher_count()) + ")");
}

}  // namespace

int main()
{
  anInfoLineArrivesWithItsLevelAndNameAndCallSite();
  everySeverityKeepsItsNumber();
  aStreamMacroProducesTheSameMessage();
  aNodeThatDeclinesTheTopicIsSilent();
  aLoggerNoNodeOwnsIsStillPublished();
  theTopicIsAdvertisedWithItsType();
  thePublisherIsGivenBackWhenTheNodeDies();

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
