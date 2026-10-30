// End-to-end test over the shared-memory shim:
//  1. run the ported nav2 navfn planner on a nav2_costmap_2d grid
//  2. publish the resulting nav_msgs::msg::Path over shared memory
//  3. drive a nav2_msgs NavigateToPose action server/client over shared memory
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav2_navfn_planner/navfn.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
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

// Spins the node until `future` is ready or `timeout` elapses. Waiting is
// bounded on purpose: a message that never arrives must not wedge the process.
template<typename NodeT, typename FutureT>
bool wait_until_ready(
  const NodeT & node, const FutureT & future, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

// Costmap geometry, in nav2_costmap_2d cell values (0 == free,
// 254 == LETHAL_OBSTACLE). The lethal wall sits at x=50 but stops at y=70 so
// the two halves of the grid stay connected; a wall spanning the whole column
// would leave the goal genuinely unreachable and NavFn would report that.
constexpr int kSize = 100;
constexpr int kWallX = 50;
constexpr int kWallGapStartY = 70;  // cells with y >= this are the doorway
constexpr int kStartX = 10, kStartY = 10;
constexpr int kGoalX = 90, kGoalY = 90;
constexpr unsigned char kLethalCost = 254;

std::vector<unsigned char> make_costmap()
{
  std::vector<unsigned char> costmap(static_cast<size_t>(kSize) * kSize, 0);
  for (int y = 0; y < kWallGapStartY; ++y) {
    costmap[static_cast<size_t>(y) * kSize + kWallX] = kLethalCost;
  }
  return costmap;
}

nav_msgs::msg::Path plan_with_navfn()
{
  auto costmap = make_costmap();

  nav2_navfn_planner::NavFn planner(kSize, kSize);
  // nav2 costmap cells use the ROS encoding, so `isROS` must stay true. This
  // mirrors nav2_navfn_planner::NavfnPlanner::makePlan(), which calls
  // setCostmap(costmap_->getCharMap(), true, params_->allow_unknown).
  planner.setCostmap(costmap.data(), true, true);
  int start[2] = {kStartX, kStartY};
  int goal[2] = {kGoalX, kGoalY};
  planner.setStart(start);
  planner.setGoal(goal);

  nav_msgs::msg::Path path;
  path.header.frame_id = "map";

  // calcNavFnAstar() only propagates the potential field and reports whether
  // the start cell became reachable. It does not produce a path on its own.
  if (!planner.calcNavFnAstar([]() {return false;})) {
    std::printf("  navfn: start cell was never reached by the propagation\n");
    return path;
  }
  // The polyline has to be extracted explicitly, exactly like nav2's planner
  // does in NavfnPlanner::getPlanFromPotential(). Without this call
  // getPathLen() keeps returning 0 even though the potential field is ready.
  const int max_cycles = kSize * 4;  // nav2: max_dimension * max_cycles_factor
  if (planner.calcPath(max_cycles) <= 0) {
    std::printf("  navfn: calcPath() could not extract a plan\n");
    return path;
  }
  const int length = planner.getPathLen();
  if (length <= 0) {
    return path;
  }
  const float * xs = planner.getPathX();
  const float * ys = planner.getPathY();
  for (int i = 0; i < length; ++i) {
    geometry_msgs::msg::PoseStamped pose;
    pose.header.frame_id = "map";
    pose.pose.position.x = xs[i];
    pose.pose.position.y = ys[i];
    pose.pose.orientation.w = 1.0;
    path.poses.push_back(pose);
  }
  return path;
}

void test_planning_and_shm()
{
  std::printf("[test] navfn planning + Path over shared memory\n");
  const auto path = plan_with_navfn();
  CHECK(path.poses.size() > 10);
  std::printf("  planned %zu waypoints\n", path.poses.size());
  if (path.poses.empty()) {
    return;
  }

  // The plan must run from the start to the goal and detour through the
  // doorway instead of crossing the lethal wall.
  const auto & first = path.poses.front().pose.position;
  const auto & last = path.poses.back().pose.position;
  std::printf("  first=(%.1f, %.1f) last=(%.1f, %.1f)\n", first.x, first.y, last.x, last.y);
  CHECK(std::fabs(first.x - kStartX) < 0.51 && std::fabs(first.y - kStartY) < 0.51);
  CHECK(std::fabs(last.x - kGoalX) < 0.51 && std::fabs(last.y - kGoalY) < 0.51);
  bool crosses_wall = false;
  bool seen_left = false;
  bool seen_right = false;
  for (const auto & pose : path.poses) {
    const auto & position = pose.pose.position;
    if (position.x < kWallX) {
      seen_left = true;
    }
    if (position.x > kWallX) {
      seen_right = true;
    }
    if (std::lround(position.x) == kWallX && std::lround(position.y) < kWallGapStartY) {
      crosses_wall = true;
    }
  }
  CHECK(seen_left && seen_right);
  CHECK(!crosses_wall);

  auto node = std::make_shared<rclcpp::Node>("e2e_planner");
  auto publisher = node->create_publisher<nav_msgs::msg::Path>("plan", 10);

  size_t received = 0;
  geometry_msgs::msg::PoseStamped last_pose;
  auto subscription = node->create_subscription<nav_msgs::msg::Path>(
    "plan", 10, [&](const nav_msgs::msg::Path::ConstSharedPtr message) {
      received = message->poses.size();
      if (!message->poses.empty()) {
        last_pose = message->poses.back();
      }
    });

  publisher->publish(path);
  for (int i = 0; i < 2000 && received == 0; ++i) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(received == path.poses.size());
  std::printf("  received %zu waypoints, last=(%.2f, %.2f)\n", received,
    last_pose.pose.position.x, last_pose.pose.position.y);
}

void test_action_over_shm()
{
  std::printf("[test] NavigateToPose action server/client over shared memory\n");
  using NavigateToPose = nav2_msgs::action::NavigateToPose;

  auto node = std::make_shared<rclcpp_lifecycle::LifecycleNode>("e2e_action");

  std::atomic<bool> server_called{false};
  auto server = rclcpp_action::create_server<NavigateToPose>(
    node->get_node_base_interface(),
    node->get_node_clock_interface(),
    node->get_node_logging_interface(),
    node->get_node_waitables_interface(),
    "navigate_to_pose",
    [](const rclcpp_action::GoalUUID &, std::shared_ptr<const NavigateToPose::Goal>) {
      return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    },
    [](std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigateToPose>>) {
      return rclcpp_action::CancelResponse::ACCEPT;
    },
    [&](std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigateToPose>> handle) {
      server_called.store(true);
      auto result = std::make_shared<NavigateToPose::Result>();
      result->error_code = NavigateToPose::Result::NONE;
      result->error_msg = "done";
      handle->succeed(result);
    });

  auto client = rclcpp_action::create_client<NavigateToPose>(node, "navigate_to_pose");

  auto goal = std::make_shared<NavigateToPose::Goal>();
  goal->pose.header.frame_id = "map";
  goal->pose.pose.position.x = 1.0;
  goal->pose.pose.position.y = 2.0;
  goal->pose.pose.orientation.w = 1.0;

  auto goal_future = client->async_send_goal(*goal);
  const bool goal_ready = wait_until_ready(node, goal_future, std::chrono::seconds(5));
  CHECK(goal_ready);
  if (!goal_ready) {
    std::printf("  goal response never arrived (bounded wait of 5s expired)\n");
    return;
  }
  auto handle = goal_future.get();
  CHECK(handle != nullptr);
  if (!handle) {
    return;
  }
  auto result_future = client->async_get_result(handle);
  const bool result_ready = wait_until_ready(node, result_future, std::chrono::seconds(5));
  CHECK(result_ready);
  if (!result_ready) {
    std::printf(
      "  result never arrived (bounded wait of 5s expired); the action client\n"
      "  discards a result that lands before async_get_result() registers a\n"
      "  promise, so the goal needs to be re-sent or the client fixed\n");
    return;
  }
  const auto wrapped = result_future.get();
  CHECK(server_called.load());
  CHECK(wrapped.code == rclcpp_action::ResultCode::SUCCEEDED);
  CHECK(wrapped.result && wrapped.result->error_msg == "done");
  std::printf(
    "  server_called=%d code=%d msg='%s'\n", server_called.load(),
    static_cast<int>(wrapped.code),
    wrapped.result ? wrapped.result->error_msg.c_str() : "");
  (void)server;
}

}  // namespace

int main()
{
  std::printf("=== end-to-end pipeline test ===\n");
  rclcpp::init(0, nullptr);
  test_planning_and_shm();
  test_action_over_shm();
  rclcpp::shutdown();
  if (g_failures == 0) {
    std::printf("=== ALL E2E TESTS PASSED ===\n");
    return 0;
  }
  std::printf("=== %d E2E CHECK(S) FAILED ===\n", g_failures);
  return 1;
}
