// `<action>/_action/status`, the seventh channel of an action.
//
// rcl_action publishes a `GoalStatusArray` for a goal that was accepted, one
// for every change of state and one when it finishes, and everything that
// watches an action without blocking on `async_get_result` reads that topic -
// `nav2_waypoint_cycle` is the caller this exists for: it subscribes to
// `navigate_to_pose/_action/status` and advances its waypoint list when it sees
// a terminal status, so an action server that never publishes the topic is one
// a mission script cannot drive from, and `ros2 topic list` does not even carry
// the name for it to subscribe to.
//
// What is pinned here: the topic is advertised in the graph with its own type,
// the status array really goes out on the channel sized for it, the array
// carries the goal's own uuid, the stamp is the acceptance time rather than the
// epoch, and the code that lands is the terminal one the result reported.
#include "action_msgs/msg/goal_status_array.hpp"
#include "nav2_msgs/action/wait.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/shm_config.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++g_failures; \
    } \
  } while (0)

using Wait = nav2_msgs::action::Wait;
using StatusArray = action_msgs::msg::GoalStatusArray;

// The action's name, deliberately not the name of anything in a running stack:
// a channel in the shim is machine-wide, so a test that reused
// `/navigate_to_pose` would read whatever the live nav2 server was doing.
constexpr const char * kAction = "status_probe";

int8_t status_of(const StatusArray & array, const rclcpp_action::GoalUUID & uuid)
{
  for (const auto & entry : array.status_list) {
    if (entry.goal_info.goal_id.uuid.size() == uuid.size() &&
      std::memcmp(entry.goal_info.goal_id.uuid.data(), uuid.data(), uuid.size()) == 0)
    {
      return entry.status;
    }
  }
  return -1;
}

// Spin the one node until the future is ready, or the deadline passes.
template<class FutureT>
bool spin_until(const std::shared_ptr<rclcpp::Node> & node, FutureT & future, int steps)
{
  for (int i = 0; i < steps; ++i) {
    if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      return true;
    }
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

void test_action_status()
{
  std::printf("[test] <action>/_action/status\n");
  auto node = std::make_shared<rclcpp::Node>("action_status_probe");

  auto server = rclcpp_action::create_server<Wait>(
    node, kAction,
    [](const rclcpp_action::GoalUUID &, std::shared_ptr<const Wait::Goal>) {
      return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    },
    [](std::shared_ptr<rclcpp_action::ServerGoalHandle<Wait>>) {
      return rclcpp_action::CancelResponse::ACCEPT;
    },
    [](std::shared_ptr<rclcpp_action::ServerGoalHandle<Wait>> handle) {
      auto feedback = std::make_shared<Wait::Feedback>();
      feedback->time_left.sec = 1;
      handle->publish_feedback(feedback);
      auto result = std::make_shared<Wait::Result>();
      result->error_code = Wait::Result::NONE;
      handle->succeed(result);
    });
  CHECK(server != nullptr);

  const std::string topic = std::string("/") + kAction + "/_action/status";

  // The topic is advertised, with the type of the message that travels on it:
  // this is what `ros2 topic list -t` prints and what a subscriber looks the
  // name up in before it decides the topic exists.
  bool advertised = false;
  for (const auto & entry : ros2_shim::graph::list_topics()) {
    if (entry.name == topic) {
      advertised = true;
      CHECK(entry.type == "action_msgs/msg/GoalStatusArray");
    }
  }
  CHECK(advertised);

  // And it goes out on the channel the sizing rule gives that name - the small
  // parameter-shaped one, because there is one of these per action server and
  // twenty of them in the nav2 stack.
  auto channel = shm_transport::ShmChannel::open(rclcpp::shim::channel_name(topic));
  CHECK(channel != nullptr);
  if (!channel) {
    return;
  }
  CHECK(channel->slot_size() == rclcpp::shim::slot_size_for_topic(topic));
  CHECK(channel->slot_size() == rclcpp::shim::parameter_slot_size());
  // Volatile, like the subscription `nav2_waypoint_cycle` makes: what is in the
  // ring before the goal is sent is not this reader's mail.
  channel->seek_to_latest();

  auto client = rclcpp_action::create_client<Wait>(node, kAction);
  CHECK(client != nullptr);
  CHECK(client->action_server_is_ready());

  Wait::Goal goal;
  goal.time.sec = 1;
  auto sent = client->async_send_goal(goal);
  CHECK(spin_until(node, sent, 2000));
  auto handle = sent.get();
  CHECK(handle != nullptr);
  if (!handle) {
    return;
  }

  auto result = client->async_get_result(handle);
  CHECK(spin_until(node, result, 2000));
  if (result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    CHECK(result.get().code == rclcpp_action::ResultCode::SUCCEEDED);
  }

  // Every array the server published, in order, until the terminal one.
  std::vector<uint8_t> buffer(channel->slot_size() + 1);
  std::vector<int8_t> seen;
  StatusArray terminal;
  bool got_terminal = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    size_t size = 0;
    if (channel->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      StatusArray message;
      if (!ros2_shim::deserialize(buffer.data(), size, message)) {
        CHECK(false);
        break;
      }
      const int8_t status = status_of(message, handle->get_goal_id());
      if (status >= 0) {
        seen.push_back(status);
        if (status == action_msgs::msg::GoalStatus::STATUS_SUCCEEDED) {
          terminal = message;
          got_terminal = true;
          break;
        }
      }
    }
  }

  CHECK(got_terminal);
  // ACCEPTED and EXECUTING come first, in that order: the array is a stream of
  // states, not just the one that happens to be last.
  CHECK(seen.size() >= 3);
  if (seen.size() >= 3) {
    CHECK(seen[0] == action_msgs::msg::GoalStatus::STATUS_ACCEPTED);
    CHECK(seen[1] == action_msgs::msg::GoalStatus::STATUS_EXECUTING);
    CHECK(seen[2] == action_msgs::msg::GoalStatus::STATUS_SUCCEEDED);
  }

  if (got_terminal) {
    CHECK(terminal.status_list.size() == 1);
    const auto & entry = terminal.status_list.front();
    CHECK(entry.status == action_msgs::msg::GoalStatus::STATUS_SUCCEEDED);
    CHECK(entry.goal_info.goal_id.uuid.size() == 16);
    // The stamp is when the goal was accepted, and this node is on the system
    // clock - so it is a real moment and not the epoch a server with no clock
    // would report.
    CHECK(entry.goal_info.stamp.sec > 0);
    std::printf(
      "  statuses=%zu terminal=%d stamp=%d.%09u goals=%zu\n", seen.size(),
      static_cast<int>(entry.status), entry.goal_info.stamp.sec,
      entry.goal_info.stamp.nanosec, terminal.status_list.size());
  }
}

}  // namespace

int main()
{
  std::printf("=== action status channel ===\n");
  rclcpp::init(0, nullptr);
  test_action_status();
  rclcpp::shutdown();
  if (g_failures == 0) {
    std::printf("=== ALL TESTS PASSED ===\n");
    return 0;
  }
  std::printf("=== %d CHECK(S) FAILED ===\n", g_failures);
  return 1;
}
