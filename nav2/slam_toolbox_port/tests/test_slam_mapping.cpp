// End-to-end SLAM over the shared-memory shim.
//
// A synthetic differential-drive robot drives one lap inside a rectangular room
// while sweeping a 360 degree laser. The test plays the part of the robot
// driver: it broadcasts `odom -> base_footprint` and publishes LaserScan
// messages on /scan, spins slam_toolbox's SynchronousSlamToolbox, and then
// checks that the occupancy grid published on /map really contains the room.
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "slam_toolbox/slam_toolbox_sync.hpp"
#include "tf2_ros/transform_broadcaster.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <thread>
#include <utility>
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

// The room: a rectangle of +/- 4.0 m by +/- 3.0 m, so every scan is bounded.
constexpr double kRoomHalfX = 4.0;
constexpr double kRoomHalfY = 3.0;
constexpr int kBeams = 360;
constexpr int kSteps = 48;      // poses per lap
constexpr double kLapRadius = 1.5;

// Distance from (x, y) to the room border along `angle`, or a large number when
// the ray leaves through a corner without crossing a wall segment.
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
  // Same frame as `base_frame`, which is what a robot with a body-mounted
  // scanner publishes and what keeps the shim's TF chain to one hop.
  scan.header.frame_id = "base_footprint";
  scan.angle_min = static_cast<float>(-M_PI);
  scan.angle_max = static_cast<float>(M_PI);
  scan.angle_increment = static_cast<float>(2.0 * M_PI / kBeams);
  scan.range_min = 0.05f;
  scan.range_max = 30.0f;
  scan.ranges.resize(kBeams);
  // ranges[i] is the distance measured at bearing (angle_min + i * increment)
  // in the sensor frame, so the ray that produced it points along that bearing
  // rotated by the scanner's yaw. Without the yaw term every scan would be
  // rotated by -yaw against the pose it is stamped with, and slam_toolbox
  // would have to fight that during matching.
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

}  // namespace

int main()
{
  rclcpp::init(0, nullptr);

  auto slam = std::make_shared<slam_toolbox::SynchronousSlamToolbox>(
    rclcpp::NodeOptions());
  // Declared before the node reads them: publishVisualizations() and setParams()
  // only fall back to their own defaults when the parameter is absent.
  slam->declare_parameter("map_update_interval", 0.05);
  slam->declare_parameter("minimum_travel_distance", 0.1);
  slam->declare_parameter("minimum_travel_heading", 0.1);
  // setParams() declares minimum_time_interval with the value it just read for
  // transform_timeout, so an unconfigured node silently drops every scan that
  // arrives less than 0.5s after the previous one. This test drives a scan
  // every ~50ms and wants all of them.
  slam->declare_parameter("transform_timeout", 0.2);
  slam->declare_parameter("minimum_time_interval", 0.02);
  slam->declare_parameter("throttle_scans", 1);

  auto driver = std::make_shared<rclcpp::Node>("slam_mapping_driver");
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

  // Ground truth of the poses we drive, keyed by the stamp carried by the scan
  // and the transform, so slam_toolbox's own estimate can be checked against
  // the pose the scan was actually taken from.
  std::map<int64_t, std::array<double, 3>> ground_truth;
  std::vector<std::array<double, 3>> estimates;
  std::vector<int64_t> estimate_stamps;
  auto pose_sub =
    driver->create_subscription<geometry_msgs::msg::PoseWithCovarianceStamped>(
    "pose", rclcpp::QoS(10),
    [&estimates, &estimate_stamps](
      geometry_msgs::msg::PoseWithCovarianceStamped::ConstSharedPtr message) {
        const auto & pose = message->pose.pose;
        const auto & q = pose.orientation;
        const double yaw = std::atan2(
          2.0 * (q.w * q.z + q.x * q.y),
          1.0 - 2.0 * (q.y * q.y + q.z * q.z));
        estimates.push_back({pose.position.x, pose.position.y, yaw});
        estimate_stamps.push_back(
          static_cast<int64_t>(message->header.stamp.sec) * 1000000000LL +
          static_cast<int64_t>(message->header.stamp.nanosec));
      });

  auto state = slam->configure();
  CHECK(state.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);
  state = slam->activate();
  CHECK(state.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);

  // The node subscribes to /scan while activating; seeing that from the
  // publisher is the shared-memory graph doing its job.
  CHECK(scan_pub->get_subscription_count() > 0);

  const auto spin_both = [&](int iterations, int sleep_ms) {
      for (int i = 0; i < iterations; ++i) {
        driver->spin_once();
        slam->spin_once();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
      }
    };

  // Drive one lap. The transform goes out first and is given a few turns of the
  // spin loop to reach the slam node's tf buffer, so each scan is matched
  // against the odometry pose it actually belongs to.
  const auto start = std::chrono::steady_clock::now();
  for (int step = 0; step < kSteps; ++step) {
    const double theta = 2.0 * M_PI * step / kSteps;
    const double x = kLapRadius * std::cos(theta);
    const double y = kLapRadius * std::sin(theta);
    const double yaw = theta + M_PI / 2.0;

    const rclcpp::Time stamp = slam->now();
    ground_truth[
      static_cast<int64_t>(stamp.nanoseconds())] = {x, y, yaw};
    tf_broadcaster.sendTransform(make_tf(x, y, yaw, stamp));
    spin_both(5, 1);

    scan_pub->publish(make_scan(x, y, yaw, stamp));
    spin_both(10, 2);
  }
  spin_both(50, 5);
  const double elapsed =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  const bool verbose = std::getenv("SLAM_MAPPING_POSES") != nullptr;
  double worst_pos = 0.0, worst_yaw = 0.0;
  int matched = 0;
  for (size_t i = 0; i < estimates.size(); ++i) {
    const auto truth = ground_truth.find(estimate_stamps[i]);
    if (truth == ground_truth.end()) {
      continue;
    }
    ++matched;
    const double dx = estimates[i][0] - truth->second[0];
    const double dy = estimates[i][1] - truth->second[1];
    worst_pos = std::max(worst_pos, std::hypot(dx, dy));
    double dyaw = estimates[i][2] - truth->second[2];
    while (dyaw > M_PI) {
      dyaw -= 2.0 * M_PI;
    }
    while (dyaw < -M_PI) {
      dyaw += 2.0 * M_PI;
    }
    worst_yaw = std::max(worst_yaw, std::fabs(dyaw));
    if (verbose) {
      std::printf(
        "    #%02zu truth=(%6.2f,%6.2f,%7.1f) slam=(%6.2f,%6.2f,%7.1f)\n",
        i, truth->second[0], truth->second[1],
        truth->second[2] * 180.0 / M_PI, estimates[i][0], estimates[i][1],
        estimates[i][2] * 180.0 / M_PI);
    }
  }
  std::printf(
    "  %zu /pose messages, %d matched to a driven pose; worst drift "
    "%.3f m / %.1f deg\n",
    estimates.size(), matched, worst_pos, worst_yaw * 180.0 / M_PI);

  CHECK(map_messages > 0);
  CHECK(latest_map.header.frame_id == "map");

  size_t occupied = 0;
  size_t known = 0;
  int min_x = std::numeric_limits<int>::max(), max_x = -1;
  int min_y = std::numeric_limits<int>::max(), max_y = -1;
  for (unsigned int row = 0; row < latest_map.info.height; ++row) {
    for (unsigned int col = 0; col < latest_map.info.width; ++col) {
      const int8_t value = latest_map.data[static_cast<size_t>(row) * latest_map.info.width + col];
      if (value < 0) {
        continue;
      }
      ++known;
      if (value >= 65) {
        ++occupied;
        min_x = std::min(min_x, static_cast<int>(col));
        max_x = std::max(max_x, static_cast<int>(col));
        min_y = std::min(min_y, static_cast<int>(row));
        max_y = std::max(max_y, static_cast<int>(row));
      }
    }
  }

  const double resolution = latest_map.info.resolution > 0.0f ?
    latest_map.info.resolution : 0.05;
  const double span_x = (max_x - min_x + 1) * resolution;
  const double span_y = (max_y - min_y + 1) * resolution;
  std::printf(
    "  %d map messages in %.1fs; grid %ux%u @ %.3fm, %zu known, %zu occupied\n",
    map_messages, elapsed, latest_map.info.width, latest_map.info.height,
    resolution, known, occupied);
  std::printf("  wall bounding box: %.2f m x %.2f m\n", span_x, span_y);

  // A blank or half-formed map is the failure mode this test exists to catch:
  // the laser has to have carved out the room's interior *and* its four walls.
  CHECK(known > 1000);
  CHECK(occupied > 200);
  // The occupied cells have to be the walls of the room that was actually
  // driven -- exactly 8 x 6 m. A scan that is stamped with one pose but built
  // from another still produces plausible-looking walls, just not these.
  CHECK(std::fabs(span_x - 2.0 * kRoomHalfX) < 0.3);
  CHECK(std::fabs(span_y - 2.0 * kRoomHalfY) < 0.3);
  // Every processed scan has to be reported at the pose it was taken from.
  CHECK(matched > 35);
  CHECK(worst_pos < 0.05);
  CHECK(worst_yaw < 0.05);

  // SLAM_MAPPING_DUMP=<path.pgm> writes the grid out for eyeballing. Rows are
  // flipped because an occupancy grid has its origin at the bottom left while
  // PGM starts at the top.
  if (const char * dump_path = std::getenv("SLAM_MAPPING_DUMP")) {
    std::ofstream out(dump_path, std::ios::binary | std::ios::trunc);
    out << "P5\n" << latest_map.info.width << " " << latest_map.info.height << "\n255\n";
    for (unsigned int row = 0; row < latest_map.info.height; ++row) {
      const unsigned int src_row = latest_map.info.height - 1 - row;
      for (unsigned int col = 0; col < latest_map.info.width; ++col) {
        const int8_t value =
          latest_map.data[static_cast<size_t>(src_row) * latest_map.info.width + col];
        const char pixel = value < 0 ? 128 : (value >= 65 ? 0 : 255);
        out.put(pixel);
      }
    }
    std::printf("  wrote %s\n", dump_path);
  }

  rclcpp::shutdown();
  slam.reset();
  driver.reset();

  if (g_failures == 0) {
    std::printf("  slam mapping: all checks passed\n");
  }
  return g_failures == 0 ? 0 : 1;
}
