// The pose-graph archive: `slam_toolbox/serialize_map` and
// `slam_toolbox/deserialize_map`.
//
// Everything the mapping test exercises ends when the process does: the graph
// it built lives in RAM and the map it published is a PNG somebody else saved.
// These two services are what make "the map I built today" into a file that a
// *later* run can pick up and keep building on -- which is the whole point of
// `serialization.hpp` writing a `.posegraph` (the nodes and constraints) next to
// a `.data` (the dataset: the laser, its pose readings).
//
// The test is a round trip, not a smoke test:
//
//   1. drive a full lap, let the map settle, remember the grid,
//   2. `serialize_map` -> RESULT_SUCCESS, and both files really on disk,
//   3. throw that node away entirely,
//   4. build a *second* node, `deserialize_map` -> the graph it publishes must
//      be the grid the first node had: same shape, same occupied/free/unknown
//      distribution, and the same cells apart from a tolerance -- if `read()`
//      came back empty or half-loaded, this is where it shows,
//   5. feed one more scan and check the room is still there, so the resumed
//      node is a working mapping node rather than a frozen picture.
//
// Why a tolerance rather than "identical", and why 0.30% of the cells.
//
// A cell-for-cell comparison is what a round trip *should* satisfy, and at the
// point this test compares it does: the grid is read immediately after
// `deserialize_map` answers, before any scan has been fed to the resumed node,
// and the measured difference is 0 cells. But "0" is then a claim about
// scheduling as much as about the archive: one `/map` message that arrives
// between `updateMap()` and the comparison, and the number is not 0 any more.
//
// The two numbers this tolerance is built from are from the live end-to-end
// runs (`03-validation.md`, evidence 41): the grid a running `slam_toolbox`
// publishes and the grid `map_server` reads back out of the saved files differ
// in **0.30%** and **0.083%** of their cells, in the two directions, with the
// robot still moving and new scans still being folded in. That is the size of a
// difference that means "the same map", so it is the number used here --
// anything larger is a map that changed, and a half-loaded archive moves far
// more than 0.3% of its cells, which is the failure this test exists for. The
// distribution check is the strict half: an archive that lost a wall moves the
// occupied count, and that is compared exactly.
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "slam_toolbox/slam_toolbox_sync.hpp"
#include "slam_toolbox/srv/deserialize_pose_graph.hpp"
#include "slam_toolbox/srv/serialize_pose_graph.hpp"
#include "tf2_ros/transform_broadcaster.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
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

// Same room, same sweep and same synthetic robot as test_slam_mapping.cpp: a
// rectangle of +/- 4.0 m by +/- 3.0 m, one lap of radius 1.5 m. The numbers are
// repeated rather than shared so that this test cannot be changed by a tweak to
// the other one.
constexpr double kRoomHalfX = 4.0;
constexpr double kRoomHalfY = 3.0;
constexpr int kBeams = 360;
constexpr int kSteps = 48;
constexpr double kLapRadius = 1.5;

double raycast(double x, double y, double angle)
{
  const double dx = std::cos(angle);
  const double dy = std::sin(angle);
  double best = std::numeric_limits<double>::max();

  if (std::fabs(dx) > 1e-9) {
    const double wall = dx > 0.0 ? kRoomHalfX : -kRoomHalfX;
    const double t = (wall - x) / dx;
    if (t > 0.0 && std::fabs(y + t * dy) <= kRoomHalfY) {
      best = std::min(best, t);
    }
  }
  if (std::fabs(dy) > 1e-9) {
    const double wall = dy > 0.0 ? kRoomHalfY : -kRoomHalfY;
    const double t = (wall - y) / dy;
    if (t > 0.0 && std::fabs(x + t * dx) <= kRoomHalfX) {
      best = std::min(best, t);
    }
  }
  return best;
}

sensor_msgs::msg::LaserScan make_scan(
  double x, double y, double yaw, const rclcpp::Time & stamp)
{
  sensor_msgs::msg::LaserScan scan;
  scan.header.stamp = stamp;
  scan.header.frame_id = "base_footprint";
  scan.angle_min = static_cast<float>(-M_PI);
  scan.angle_max = static_cast<float>(M_PI);
  scan.angle_increment = static_cast<float>(2.0 * M_PI / kBeams);
  scan.range_min = 0.05f;
  scan.range_max = 30.0f;
  scan.ranges.resize(kBeams);
  for (int i = 0; i < kBeams; ++i) {
    const double angle = yaw + scan.angle_min + i * scan.angle_increment;
    scan.ranges[i] = static_cast<float>(raycast(x, y, angle));
  }
  return scan;
}

geometry_msgs::msg::TransformStamped make_tf(
  double x, double y, double yaw, const rclcpp::Time & stamp)
{
  geometry_msgs::msg::TransformStamped msg;
  msg.header.stamp = stamp;
  msg.header.frame_id = "odom";
  msg.child_frame_id = "base_footprint";
  msg.transform.translation.x = x;
  msg.transform.translation.y = y;
  msg.transform.rotation.z = std::sin(yaw / 2.0);
  msg.transform.rotation.w = std::cos(yaw / 2.0);
  return msg;
}

// The parameters the mapping test declares, for the reason it declares them.
std::shared_ptr<slam_toolbox::SynchronousSlamToolbox> make_slam()
{
  auto slam = std::make_shared<slam_toolbox::SynchronousSlamToolbox>(
    rclcpp::NodeOptions());
  slam->declare_parameter("map_update_interval", 0.05);
  slam->declare_parameter("minimum_travel_distance", 0.1);
  slam->declare_parameter("minimum_travel_heading", 0.1);
  slam->declare_parameter("transform_timeout", 0.2);
  slam->declare_parameter("minimum_time_interval", 0.02);
  slam->declare_parameter("throttle_scans", 1);
  return slam;
}

// How two occupancy grids differ, in the terms that matter here: a grid that
// came back with a different size, origin or cell values is not "roughly the
// same map", it is a different map.
struct GridDiff
{
  int differing = 0;
  bool same_shape = false;
  std::string shape_a;
  std::string shape_b;
};

GridDiff compare_grids(
  const nav_msgs::msg::OccupancyGrid & a, const nav_msgs::msg::OccupancyGrid & b)
{
  GridDiff diff;
  char buffer[160];
  std::snprintf(
    buffer, sizeof(buffer), "%ux%u @ %.3fm origin (%.3f, %.3f)",
    a.info.width, a.info.height, a.info.resolution, a.info.origin.position.x,
    a.info.origin.position.y);
  diff.shape_a = buffer;
  std::snprintf(
    buffer, sizeof(buffer), "%ux%u @ %.3fm origin (%.3f, %.3f)",
    b.info.width, b.info.height, b.info.resolution, b.info.origin.position.x,
    b.info.origin.position.y);
  diff.shape_b = buffer;

  diff.same_shape =
    a.info.width == b.info.width && a.info.height == b.info.height &&
    std::fabs(a.info.resolution - b.info.resolution) < 1e-9 &&
    std::fabs(a.info.origin.position.x - b.info.origin.position.x) < 1e-9 &&
    std::fabs(a.info.origin.position.y - b.info.origin.position.y) < 1e-9;

  if (a.data.size() != b.data.size()) {
    diff.differing = static_cast<int>(std::max(a.data.size(), b.data.size()));
    return diff;
  }
  for (size_t i = 0; i < a.data.size(); ++i) {
    if (a.data[i] != b.data[i]) {
      ++diff.differing;
    }
  }
  return diff;
}

int count_occupied(const nav_msgs::msg::OccupancyGrid & grid)
{
  int occupied = 0;
  for (int8_t value : grid.data) {
    if (value >= 65) {
      ++occupied;
    }
  }
  return occupied;
}

// The distribution a restored grid has to reproduce: ROS2's three states, in
// the encoding `nav_msgs/OccupancyGrid` defines - 0 free, 100 occupied, -1
// unknown.
struct GridCounts
{
  int occupied = 0;
  int free_cells = 0;
  int unknown = 0;
};

GridCounts count_cells(const nav_msgs::msg::OccupancyGrid & grid)
{
  GridCounts counts;
  for (int8_t value : grid.data) {
    if (value < 0) {
      ++counts.unknown;
    } else if (value == 0) {
      ++counts.free_cells;
    } else {
      ++counts.occupied;
    }
  }
  return counts;
}

// How much of a grid is allowed to move and still be "the same map": 0.30% of
// its cells, the larger of the two end-to-end measurements in the header.
bool within_tolerance(const GridDiff & diff, size_t cells)
{
  return static_cast<long long>(diff.differing) * 1000 <=
         static_cast<long long>(cells) * 3;
}

double percent(int part, size_t whole)
{
  return whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
}

// Size of a file on disk, or 0 when it is not there.
long long file_size(const std::string & path)
{
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  return in ? static_cast<long long>(in.tellg()) : 0LL;
}

const char * temp_dir()
{
  const char * dir = std::getenv("TEMP");
  if (dir == nullptr) {
    dir = std::getenv("TMP");
  }
  return dir != nullptr ? dir : ".";
}

}  // namespace

int main()
{
  rclcpp::init(0, nullptr);

  const std::string base =
    std::string(temp_dir()) + "/slam_serialize_roundtrip";
  const std::string posegraph = base + ".posegraph";
  const std::string dataset = base + ".data";
  // A leftover file from an interrupted run would make the "it wrote something"
  // check pass for the wrong reason.
  std::remove(posegraph.c_str());
  std::remove(dataset.c_str());
  std::printf("  archive: %s\n", base.c_str());

  auto driver = std::make_shared<rclcpp::Node>("slam_serialize_driver");
  tf2_ros::TransformBroadcaster tf_broadcaster(driver);
  auto scan_pub =
    driver->create_publisher<sensor_msgs::msg::LaserScan>("/scan", rclcpp::QoS(10));

  nav_msgs::msg::OccupancyGrid latest_map;
  int map_messages = 0;
  auto map_sub = driver->create_subscription<nav_msgs::msg::OccupancyGrid>(
    "/map", rclcpp::QoS(1),
    [&latest_map, &map_messages](nav_msgs::msg::OccupancyGrid::ConstSharedPtr message) {
      latest_map = *message;
      ++map_messages;
    });

  auto client_node = std::make_shared<rclcpp::Node>("slam_serialize_client");

  // ---------------------------------------------------------------- phase 1
  std::printf("  phase 1: map one lap, then serialize it\n");
  auto slam = make_slam();
  const auto spin_both = [&](int iterations, int sleep_ms) {
      for (int i = 0; i < iterations; ++i) {
        driver->spin_once();
        slam->spin_once();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
      }
    };

  auto state = slam->configure();
  CHECK(state.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
  state = slam->activate();
  CHECK(state.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);

  for (int step = 0; step < kSteps; ++step) {
    const double theta = 2.0 * M_PI * step / kSteps;
    const double x = kLapRadius * std::cos(theta);
    const double y = kLapRadius * std::sin(theta);
    const double yaw = theta + M_PI / 2.0;
    const rclcpp::Time stamp = slam->now();
    tf_broadcaster.sendTransform(make_tf(x, y, yaw, stamp));
    spin_both(5, 1);
    scan_pub->publish(make_scan(x, y, yaw, stamp));
    spin_both(10, 2);
  }
  spin_both(50, 5);

  const nav_msgs::msg::OccupancyGrid driven_map = latest_map;
  const int driven_occupied = count_occupied(driven_map);
  std::printf(
    "  before serializing: %d map messages, %ux%u, %d occupied cells\n",
    map_messages, driven_map.info.width, driven_map.info.height, driven_occupied);
  CHECK(map_messages > 0);
  CHECK(driven_occupied > 200);

  auto serialize_client =
    client_node->create_client<slam_toolbox::srv::SerializePoseGraph>(
    "slam_toolbox/serialize_map");
  CHECK(serialize_client->wait_for_service(std::chrono::seconds(5)));

  auto serialize_request =
    std::make_shared<slam_toolbox::srv::SerializePoseGraph::Request>();
  serialize_request->filename = base;
  auto serialize_future = serialize_client->async_send_request(serialize_request);
  for (int i = 0;
    i < 4000 &&
    serialize_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready; ++i)
  {
    slam->spin_once();
    client_node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(serialize_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
  if (serialize_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    const auto response = serialize_future.get();
    std::printf("  serialize_map -> result %u\n", static_cast<unsigned>(response->result));
    CHECK(response->result == slam_toolbox::srv::SerializePoseGraph::Response::RESULT_SUCCESS);
  }

  // The services report success or failure; whether there is a file is a
  // separate question, and the one a user cares about.
  const long long posegraph_size = file_size(posegraph);
  const long long dataset_size = file_size(dataset);
  std::printf(
    "  on disk: %s %lld bytes, %s %lld bytes\n", posegraph.c_str(),
    posegraph_size, dataset.c_str(), dataset_size);
  CHECK(posegraph_size > 0);
  CHECK(dataset_size > 0);

  // An empty archive would still be a file, so the read-back below is what
  // really decides whether the write is worth anything.
  slam.reset();

  // ---------------------------------------------------------------- phase 2
  std::printf("  phase 2: a fresh node deserializes that archive\n");
  auto resumed = make_slam();
  // The `/map` subscription `driver` already has stays the only one, and its
  // counter starts over here. The shim hands a node one channel - so one read
  // cursor - per topic (rclcpp/node.hpp, get_private_channel), so a second
  // subscription to `/map` on this node would take turns with the first one
  // eating the messages instead of both seeing them. Counting from zero
  // measures the same thing: what `driver` was told after the load.
  map_messages = 0;

  const auto spin_resumed = [&](int iterations, int sleep_ms) {
      for (int i = 0; i < iterations; ++i) {
        driver->spin_once();
        resumed->spin_once();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
      }
    };

  state = resumed->configure();
  CHECK(state.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
  state = resumed->activate();
  CHECK(state.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);

  auto deserialize_client =
    client_node->create_client<slam_toolbox::srv::DeserializePoseGraph>(
    "slam_toolbox/deserialize_map");
  CHECK(deserialize_client->wait_for_service(std::chrono::seconds(5)));

  auto deserialize_request =
    std::make_shared<slam_toolbox::srv::DeserializePoseGraph::Request>();
  deserialize_request->filename = base;
  deserialize_request->match_type =
    slam_toolbox::srv::DeserializePoseGraph::Request::START_AT_FIRST_NODE;
  auto deserialize_future = deserialize_client->async_send_request(deserialize_request);
  for (int i = 0;
    i < 4000 &&
    deserialize_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready; ++i)
  {
    resumed->spin_once();
    client_node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(deserialize_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
  if (deserialize_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    deserialize_future.get();
  }
  // deserializePoseGraphCallback() calls updateMap(), so the grid it rebuilt
  // from the archive is published by the time the call returns. No scan has
  // been fed to this node yet.
  spin_resumed(50, 2);

  std::printf("  resumed node published %d map message(s)\n", map_messages);
  CHECK(map_messages > 0);

  const GridDiff diff = compare_grids(driven_map, latest_map);
  const GridCounts before_cells = count_cells(driven_map);
  const GridCounts after_cells = count_cells(latest_map);
  const size_t total = latest_map.data.size();
  std::printf("  before  : %s\n", diff.shape_a.c_str());
  std::printf("  after   : %s\n", diff.shape_b.c_str());
  std::printf(
    "  before  : %d occupied, %d free, %d unknown\n", before_cells.occupied,
    before_cells.free_cells, before_cells.unknown);
  std::printf(
    "  after   : %d occupied, %d free, %d unknown\n", after_cells.occupied,
    after_cells.free_cells, after_cells.unknown);

  CHECK(diff.same_shape);
  // The distribution, exactly. This is the half that cannot be argued with: a
  // graph that did not come back loses walls, and walls are occupied cells.
  CHECK(before_cells.occupied == after_cells.occupied);
  CHECK(before_cells.free_cells == after_cells.free_cells);
  CHECK(before_cells.unknown == after_cells.unknown);
  std::printf(
    "  differing cells: %d of %zu (%.3f%%)\n", diff.differing, total,
    percent(diff.differing, total));
  CHECK(within_tolerance(diff, total));

  // ------------------------------------------------------------- continues
  // The point of loading a graph is to keep mapping into it. One more pose on
  // the same circle, and the room has to be intact afterwards -- a node that
  // deserialized a picture rather than a graph loses the walls the moment it
  // processes a scan.
  std::printf("  continuing: one more scan at the end of the lap\n");
  const int before_continue = count_occupied(latest_map);
  const double theta = 2.0 * M_PI * (kSteps - 1) / kSteps;
  const double x = kLapRadius * std::cos(theta);
  const double y = kLapRadius * std::sin(theta);
  const double yaw = theta + M_PI / 2.0;
  const rclcpp::Time stamp = resumed->now();
  tf_broadcaster.sendTransform(make_tf(x, y, yaw, stamp));
  spin_resumed(10, 2);
  scan_pub->publish(make_scan(x, y, yaw, stamp));
  spin_resumed(60, 5);

  const int after_continue = count_occupied(latest_map);
  std::printf(
    "  occupied cells: %d after deserializing, %d after one more scan\n",
    before_continue, after_continue);
  // The same tolerance against the grid that was archived, now with a scan that
  // really has been folded in: the cells that scan observed may have moved, and
  // nothing else may have. Losing the map is what would show up here - it takes
  // every wall with it.
  const GridDiff continued = compare_grids(driven_map, latest_map);
  const size_t continued_total = latest_map.data.size();
  std::printf(
    "  after one more scan: %d cells differ from the archived grid (%.3f%%)\n",
    continued.differing, percent(continued.differing, continued_total));
  CHECK(within_tolerance(continued, continued_total));
  CHECK(after_continue > before_continue / 2);
  std::printf("  final grid: %ux%u, %d occupied\n",
    latest_map.info.width, latest_map.info.height, after_continue);

  rclcpp::shutdown();
  resumed.reset();
  slam.reset();
  driver.reset();
  client_node.reset();
  std::remove(posegraph.c_str());
  std::remove(dataset.c_str());

  if (g_failures == 0) {
    std::printf("  slam serialization: all checks passed\n");
  }
  return g_failures == 0 ? 0 : 1;
}
