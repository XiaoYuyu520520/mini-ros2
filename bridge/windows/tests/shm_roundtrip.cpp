// bridge_shm_roundtrip - the shared-memory half of the bridge, with no daemon
// and no network.
//
// `ros2_bridge/shm_topic.hpp` is the whole contract between this tree and the
// shim's channels: the name a topic maps to, how big the slots on it are, the
// `_request` / `_response` halves of a service and the six channels of an
// action. `ros2_bridge_daemon` and `ros2` both go through it, and when it
// disagrees with what `nav2/ros2_shim` created, the symptom is a subscriber
// that never wakes up rather than an error anybody can read.
//
// `CMakeLists.txt` in this directory has registered this test since before the
// file existed - `tests/shm_roundtrip.cpp` sat inside an `if(EXISTS)`, so it
// silently never registered and the tree ran four tests while the file listed
// five. This is that file, and what it pins is:
//
//   1. the names, the `NAV2_SHM_NAMESPACE` prefix included;
//   2. the slot size each name gets, straight out of `rclcpp/shm_config.hpp`,
//      including the two places the rule is subtle: a `..._info` topic, and a
//      parameter service's channel path;
//   3. a publish/drain round trip through two handles - the writer a node's
//      publisher opens and the reader the daemon opens on the same topic -
//      byte exact, in order, with nothing dropped;
//   4. the three things that must not be silent: a message that exactly fills
//      a slot, one that does not fit in one, and a channel nobody created.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "ros2_bridge/shm_topic.hpp"

namespace
{

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond) \
  do { \
    ++g_checks; \
    if (!(cond)) { \
      std::fprintf(stderr, "  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++g_failures; \
    } \
  } while (0)

void failMessage(const std::string & message)
{
  ++g_failures;
  std::fprintf(stderr, "  FAIL %s\n", message.c_str());
}

void expectTrue(const std::string & what, bool actual)
{
  ++g_checks;
  if (!actual) {
    failMessage(what);
  }
}

void expectEqual(
  const std::string & what, const std::string & actual, const std::string & expected)
{
  ++g_checks;
  if (actual != expected) {
    failMessage(what + ": got \"" + actual + "\", expected \"" + expected + "\"");
  }
}

void expectEqualSize(const std::string & what, std::size_t actual, std::size_t expected)
{
  ++g_checks;
  if (actual != expected) {
    failMessage(
      what + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
  }
}

// A name nothing else on the machine is using.
//
// CTest gives this test a namespace of its own (the loop at the end of
// `CMakeLists.txt`), which keeps it away from the live graph - but not from an
// earlier run of this same executable that died before its channels were
// cleaned up. The cases that ask what happens when a channel does *not* exist
// yet need a name that is fresh by construction, so they carry something unique
// to this process as well.
std::string unique_name(const char * what)
{
  static int counter = 0;
  const std::uintptr_t salt = reinterpret_cast<std::uintptr_t>(&counter);
  return "/shm_roundtrip/" + std::string(what) + "-" + std::to_string(salt) + "-" +
         std::to_string(++counter);
}

// The prefix `rclcpp::shim::channel_name` puts in front of every topic, read
// from the same variable it reads: with the value CTest sets, the names of this
// run cannot be the names of the live daemon's.
std::string namespace_prefix()
{
  const char * const ns = std::getenv("NAV2_SHM_NAMESPACE");
  return std::string("ros2shim") + ((ns != nullptr) ? ns : "");
}

void test_names()
{
  std::printf("[test] the namespace every channel name carries\n");
  const std::string prefix = namespace_prefix();

  expectEqual("topic /scan", ros2_bridge::channel_name("/scan"), prefix + "/scan");
  expectEqual("topic /cmd_vel", ros2_bridge::channel_name("/cmd_vel"), prefix + "/cmd_vel");
  expectEqual("service request",
    ros2_bridge::channel_name(ros2_bridge::service_channel("/map_saver/save_map", "request")),
    prefix + "/map_saver/save_map/_request");
  expectEqual("service response",
    ros2_bridge::channel_name(ros2_bridge::service_channel("/map_saver/save_map", "response")),
    prefix + "/map_saver/save_map/_response");

  // The six channels of an action, spelled the way `rclcpp_action` spells
  // them: the part is a suffix of the name, not a path segment of its own.
  const char * const parts[] = {
    "goal", "goal_ack", "cancel", "cancel_ack", "feedback", "result"};
  for (const char * part : parts) {
    expectEqual(std::string("action ") + part,
      ros2_bridge::channel_name(ros2_bridge::action_channel("/navigate_to_pose", part)),
      prefix + "/navigate_to_pose/_" + part);
  }
}

void test_slot_sizes()
{
  std::printf("[test] the sizing rule, as it comes out of the shim\n");
  using rclcpp::shim::default_slot_size;
  using rclcpp::shim::large_slot_size;
  using rclcpp::shim::parameter_slot_size;

  expectEqualSize("/scan takes the default", ros2_bridge::slot_size_for("/scan"), default_slot_size());
  expectEqualSize("/cmd_vel takes the default", ros2_bridge::slot_size_for("/cmd_vel"), default_slot_size());
  expectEqualSize("/map takes a large slot", ros2_bridge::slot_size_for("/map"), large_slot_size());
  expectEqualSize("/global_costmap/costmap takes a large slot",
    ros2_bridge::slot_size_for("/global_costmap/costmap"), large_slot_size());
  expectEqualSize("/local_costmap/voxel_grid takes a large slot",
    ros2_bridge::slot_size_for("/local_costmap/voxel_grid"), large_slot_size());

  // A topic whose name says it carries `..._info` is a handful of numbers
  // however much "costmap" is in the address - the exception the shim spells
  // out, because two of those were a gigabyte of ring per costmap-owning
  // process.
  expectEqualSize("/keepout_costmap_filter_info takes the default",
    ros2_bridge::slot_size_for("/keepout_costmap_filter_info"), default_slot_size());

  expectEqualSize("/parameter_events takes the parameter size",
    ros2_bridge::slot_size_for("/parameter_events"), parameter_slot_size());
  expectEqualSize("/rosout takes the parameter size",
    ros2_bridge::slot_size_for("/rosout"), parameter_slot_size());
  expectEqualSize("a parameter service takes the parameter size",
    ros2_bridge::slot_size_for("/controller_server/get_parameters"), parameter_slot_size());

  // One of these exists per action server and there are twenty of them in the
  // nav2 stack, so the status array takes the small size for the same reason
  // `/rosout` does.
  expectEqualSize("an action status topic takes the parameter size",
    ros2_bridge::slot_size_for("/navigate_to_pose/_action/status"), parameter_slot_size());
  expectEqualSize("and so does one on a relative name",
    ros2_bridge::slot_size_for("/spin/_action/status"), parameter_slot_size());

  // And the reason `create_service_channel` sizes from the service name rather
  // than from the channel it lives on: the path is not recognised as a
  // parameter service at all, so sizing it that way would disagree with the
  // node's own `create_service` and the slot the two ends ring would differ.
  expectEqualSize("its channel path does not",
    ros2_bridge::slot_size_for("/controller_server/get_parameters/_request"), default_slot_size());
}

void test_create_paths()
{
  std::printf("[test] what create hands back is sized by the same rule\n");
  std::string error;

  const std::string topic = unique_name("probe");
  auto channel = ros2_bridge::create_channel(topic, &error);
  CHECK(channel != nullptr);
  if (!channel) {
    failMessage("create_channel(" + topic + "): " + error);
    return;
  }
  expectEqualSize("a topic's channel is sized from the topic name",
    channel->slot_size(), ros2_bridge::slot_size_for(topic));

  // A service is sized from the *service* name, never from the `_request` path
  // it lives on: for a parameter service those two disagree (256 KB against the
  // 2 MB default), and it is the size the node's own `create_service` used that
  // has to match.
  const std::string service = unique_name("sized_service") + "/get_parameters";
  auto request = ros2_bridge::create_service_channel(service, "request", &error);
  CHECK(request != nullptr);
  if (request) {
    expectEqualSize("a service's channel is sized from the service name",
      request->slot_size(), rclcpp::shim::parameter_slot_size());
    expectTrue("which is not what its channel path would have been sized",
      ros2_bridge::slot_size_for(ros2_bridge::service_channel(service, "request")) !=
        request->slot_size());
  }

  // An action is sized from the channel path - that is the string
  // `rclcpp_action::Client` and `Server` hand to `Node::get_channel`, so both
  // ends arrive at the same layout without either knowing the action's name.
  const std::string action = unique_name("sized_action");
  auto goal = ros2_bridge::create_action_channel(action, "goal", &error);
  CHECK(goal != nullptr);
  if (goal) {
    expectEqualSize("an action's channel is sized from the channel path",
      goal->slot_size(), ros2_bridge::slot_size_for(ros2_bridge::action_channel(action, "goal")));
  }
}

void test_roundtrip()
{
  std::printf("[test] writer handle to reader handle, byte exact\n");
  std::string error;
  const std::string topic = unique_name("scan");

  // The writer a node's publisher opens...
  auto writer = ros2_bridge::create_channel(topic, &error);
  CHECK(writer != nullptr);
  // ...and the reader the daemon opens on the same topic, before anything is
  // published, which is the order a subscriber sees them in.
  auto reader = ros2_bridge::open_channel(topic, &error);
  CHECK(reader != nullptr);
  if (!writer || !reader) {
    failMessage("create/open " + topic + ": " + error);
    return;
  }

  const std::size_t sizes[] = {1, 42, 4096, 65536};
  std::vector<ros2_bridge::Bytes> sent;
  for (std::size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    ros2_bridge::Bytes message(sizes[i]);
    for (std::size_t b = 0; b < message.size(); ++b) {
      message[b] = static_cast<std::uint8_t>((i * 131 + b * 7) & 0xFF);
    }
    CHECK(writer->publish(message.data(), message.size()));
    sent.push_back(std::move(message));
  }

  std::vector<ros2_bridge::Bytes> received;
  expectEqualSize("every message came back",
    ros2_bridge::drain_channel(*reader, received), sent.size());
  if (received.size() == sent.size()) {
    for (std::size_t i = 0; i < sent.size(); ++i) {
      expectTrue("message " + std::to_string(i) + " is byte exact", received[i] == sent[i]);
    }
  }
  expectEqualSize("and the reader dropped none of them", reader->dropped_count(), 0);

  // The drain is a poll, not a wait: a second call has nothing to do and says
  // so instead of blocking.
  std::vector<ros2_bridge::Bytes> again;
  expectEqualSize("a second drain finds an empty channel",
    ros2_bridge::drain_channel(*reader, again), 0);
}

void test_slot_boundaries()
{
  std::printf("[test] a full slot survives, an oversized publish is refused\n");
  std::string error;
  const std::string topic = unique_name("boundary");

  auto writer = ros2_bridge::create_channel(topic, &error);
  auto reader = ros2_bridge::open_channel(topic, &error);
  CHECK(writer != nullptr);
  CHECK(reader != nullptr);
  if (!writer || !reader) {
    failMessage("create/open " + topic + ": " + error);
    return;
  }

  // Exactly one slot's worth. This is the case `drain_channel` sizes its buffer
  // for, and the one a buffer that was too small would consume and silently
  // drop rather than report.
  const std::uint32_t slot_size = writer->slot_size();
  ros2_bridge::Bytes full(slot_size);
  for (std::size_t b = 0; b < full.size(); ++b) {
    full[b] = static_cast<std::uint8_t>(b & 0xFF);
  }
  CHECK(writer->publish(full.data(), full.size()));

  std::vector<ros2_bridge::Bytes> received;
  expectEqualSize("the full slot arrived", ros2_bridge::drain_channel(*reader, received), 1);
  if (received.size() == 1) {
    expectTrue("with every byte of it", received[0] == full);
  }

  // One byte more than a slot does not fit, and `publish` says so rather than
  // truncating the message or waiting for a slot that will never be big enough.
  ros2_bridge::Bytes oversized(static_cast<std::size_t>(slot_size) + 1, 0x5A);
  expectTrue("an oversized publish is refused",
    !writer->publish(oversized.data(), oversized.size()));
  std::vector<ros2_bridge::Bytes> extra;
  expectEqualSize("and it never reaches the channel",
    ros2_bridge::drain_channel(*reader, extra), 0);
}

void test_open_semantics()
{
  std::printf("[test] open says nothing is there, open_or_create makes it\n");
  std::string error;

  const std::string absent = unique_name("absent");
  expectTrue("opening a channel nobody created fails",
    ros2_bridge::open_channel(absent, &error) == nullptr);

  // The rule the shim uses for a channel two processes may both be first to
  // touch: the second one opens what the first created instead of failing.
  const std::string service = unique_name("late_service");
  auto response = ros2_bridge::open_or_create_service_channel(service, "response", &error);
  expectTrue("open_or_create creates when nothing is there", response != nullptr);
  if (response) {
    expectTrue("and the channel is openable by name afterwards",
      ros2_bridge::open_channel(
        ros2_bridge::service_channel(service, "response"), &error) != nullptr);
  }

  const std::string action = unique_name("late_action");
  auto result = ros2_bridge::open_or_create_action_channel(action, "result", &error);
  expectTrue("the action half behaves the same way", result != nullptr);
  if (result) {
    expectTrue("and is openable by name too",
      ros2_bridge::open_channel(ros2_bridge::action_channel(action, "result"), &error) != nullptr);
  }
}

}  // namespace

int main()
{
  test_names();
  test_slot_sizes();
  test_create_paths();
  test_roundtrip();
  test_slot_boundaries();
  test_open_semantics();

  if (g_failures != 0) {
    std::fprintf(stderr, "\n%d of %d checks failed\n", g_failures, g_checks);
    return 1;
  }
  std::printf("\nshm roundtrip: %d checks passed\n", g_checks);
  return 0;
}
