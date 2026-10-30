// What the `/scan` -> slam path looks like from inside the shim.
//
// slam_toolbox drives its scans through `message_filters::Subscriber` ->
// `tf2_ros::MessageFilter` (target `odom`, tolerance `transform_timeout`) and
// then asks the transform buffer for the robot's pose *at the instant stamped
// on the scan*. This reproduces exactly that, one scan per driven pose, and
// prints the three numbers that decide whether slam can use the scan:
//
//   * the stamp the driver put on the scan and the wall-clock gap to the
//     previous one (slam drops anything closer than `minimum_time_interval`),
//   * how long the filter held it (a held message is one the filter decided was
//     not transformable yet),
//   * the pose the buffer answers at that stamp, and how far it moved from the
//     previous one (slam drops anything that moved less than
//     `minimum_travel_distance`).
//
// It runs against the shared-memory graph, like the test it mirrors.
//
//   scan_path_probe.exe [--steps N] [--radius R] [--tolerance S] [--quiet]

#include "rclcpp/rclcpp.hpp"
#include "message_filters/subscriber.hpp"
#include "tf2_ros/buffer.hpp"
#include "tf2_ros/message_filter.hpp"
#include "tf2_ros/transform_broadcaster.hpp"
#include "tf2_ros/transform_listener.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

int steps = 48;
double radius = 1.5;
double tolerance = 0.2;
bool quiet = false;

struct Sample
{
  int index = 0;
  double stamp = 0.0;
  double stamp_gap = 0.0;
  double held_ms = 0.0;
  double x = 0.0, y = 0.0, yaw = 0.0;
  double moved = 0.0;
  bool pose_ok = false;
};

std::vector<Sample> samples;

sensor_msgs::msg::LaserScan make_scan(const rclcpp::Time & stamp)
{
  sensor_msgs::msg::LaserScan scan;
  scan.header.stamp = stamp;
  scan.header.frame_id = "base_footprint";
  scan.angle_min = 0.0f;
  scan.angle_max = 0.0f;
  scan.angle_increment = 0.1f;
  scan.range_min = 0.05f;
  scan.range_max = 30.0f;
  scan.ranges.push_back(1.0f);
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

double stamp_seconds(const rclcpp::Time & t)
{
  return static_cast<double>(t.nanoseconds()) * 1e-9;
}

}  // namespace

int main(int argc, char ** argv)
{
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
      steps = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--radius") == 0 && i + 1 < argc) {
      radius = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--tolerance") == 0 && i + 1 < argc) {
      tolerance = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--quiet") == 0) {
      quiet = true;
    }
  }

  rclcpp::init(0, nullptr);
  auto driver = std::make_shared<rclcpp::Node>("scan_path_driver");
  auto robot = std::make_shared<rclcpp::Node>("scan_path_robot");

  tf2_ros::TransformBroadcaster broadcaster(driver);
  // The slam node publishes `map -> odom` on a timer of its own (20 Hz here,
  // `transform_publish_period` upstream) while the driver publishes odometry at
  // the rate the scans come in. Two broadcasters, one /tf topic.
  auto map_frame_node = std::make_shared<rclcpp::Node>("scan_path_map_publisher");
  tf2_ros::TransformBroadcaster map_odom_broadcaster(map_frame_node);
  auto scan_pub =
    driver->create_publisher<sensor_msgs::msg::LaserScan>("/scan", rclcpp::QoS(10));

  // The robot side, spelled the way slam_toolbox spells it.
  // slam_toolbox's own setup: a listener with a private node and its own spin
  // thread, whose buffer `pose_utils::GetPoseHelper` then reads.
  auto tf = std::make_shared<tf2_ros::Buffer>();
  auto tf_listener = std::make_shared<tf2_ros::TransformListener>(*tf);
  message_filters::Subscriber<sensor_msgs::msg::LaserScan, rclcpp::Node> scan_sub(
    robot.get(), "/scan", rmw_qos_profile_sensor_data);
  tf2_ros::MessageFilter<sensor_msgs::msg::LaserScan> filter(
    scan_sub, *tf, "odom", 10, robot, tf2::durationFromSec(tolerance));

  auto last_delivery = std::make_shared<std::chrono::steady_clock::time_point>();
  int next_index = 0;
  filter.registerCallback(
    [tf, last_delivery, &next_index](const sensor_msgs::msg::LaserScan::ConstSharedPtr & scan) {
      Sample sample;
      sample.index = next_index++;
      sample.stamp = stamp_seconds(rclcpp::Time(scan->header.stamp));
      sample.held_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - *last_delivery).count();
      *last_delivery = std::chrono::steady_clock::now();

      geometry_msgs::msg::TransformStamped identity;
      identity.header.stamp = scan->header.stamp;
      identity.header.frame_id = scan->header.frame_id;
      identity.transform.rotation.w = 1.0;
      try {
        const auto in_odom = tf->transform(identity, "odom");
        sample.x = in_odom.transform.translation.x;
        sample.y = in_odom.transform.translation.y;
        sample.yaw = std::atan2(
          2.0 * in_odom.transform.rotation.w * in_odom.transform.rotation.z,
          1.0 - 2.0 * in_odom.transform.rotation.z * in_odom.transform.rotation.z);
        sample.pose_ok = true;
      } catch (const tf2::TransformException &) {
      }
      // The same relation asked for the other way round, plus the transform of
      // the frame the scan was stamped with: three views of one lookup.
      try {
        const auto direct = tf->lookupTransform("odom", scan->header.frame_id, scan->header.stamp);
        std::printf(
          "    lookupTransform(odom, %s) = (%7.3f, %7.3f, %7.3f)\n",
          scan->header.frame_id.c_str(), direct.transform.translation.x,
          direct.transform.translation.y, direct.transform.translation.z);
      } catch (const tf2::TransformException & e) {
        std::printf("    lookupTransform(odom, %s) FAILED: %s\n",
          scan->header.frame_id.c_str(), e.what());
      }
      if (!samples.empty()) {
        sample.stamp_gap = (sample.stamp - samples.back().stamp) * 1e3;
        sample.moved = std::hypot(sample.x - samples.back().x, sample.y - samples.back().y);
      }
      samples.push_back(sample);
    });

  const auto spin_both = [&](int iterations, int sleep_ms) {
      for (int i = 0; i < iterations; ++i) {
        driver->spin_once();
        robot->spin_once();
        map_frame_node->spin_once();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
      }
    };

  for (int step = 0; step < steps; ++step) {
    const double theta = 2.0 * M_PI * step / steps;
    const double x = radius * std::cos(theta);
    const double y = radius * std::sin(theta);
    const double yaw = theta + M_PI / 2.0;
    const rclcpp::Time stamp = robot->now();
    broadcaster.sendTransform(make_tf(x, y, yaw, stamp));
    // `map -> odom`, stamped the way slam stamps it: the scan's instant plus
    // the transform timeout.
    geometry_msgs::msg::TransformStamped map_to_odom = make_tf(0.0, 0.0, 0.0, stamp);
    map_to_odom.header.frame_id = "map";
    map_to_odom.child_frame_id = "odom";
    map_odom_broadcaster.sendTransform(map_to_odom);
    spin_both(5, 1);
    scan_pub->publish(make_scan(stamp));
    spin_both(10, 2);
  }
  spin_both(50, 5);

  std::printf("== scan_path_probe: %zu of %d scans delivered ==\n", samples.size(), steps);
  double worst_gap = 0.0, worst_held = 0.0, worst_stamp_extra = 0.0;
  int untransformable = 0, too_close = 0, too_short = 0;
  for (size_t i = 0; i < samples.size(); ++i) {
    const Sample & s = samples[i];
    if (!quiet) {
      std::printf(
        "  #%02d stamp %.3f  stamp gap %7.1f ms  held %6.1f ms  pose %s "
        "(%6.3f, %6.3f, %7.1f deg)  moved %.3f m\n",
        s.index, s.stamp, s.stamp_gap, s.held_ms, s.pose_ok ? "ok " : "MISSING",
        s.x, s.y, s.yaw * 180.0 / M_PI, s.moved);
    }
    if (!s.pose_ok) {
      ++untransformable;
    }
    if (i > 0) {
      worst_gap = std::max(worst_gap, s.stamp_gap);
      worst_stamp_extra = std::max(worst_stamp_extra, std::fabs(s.stamp_gap - 30.0));
      if (s.stamp_gap < 20.0) {
        ++too_close;
      }
      if (s.moved < 0.1) {
        ++too_short;
      }
    }
    worst_held = std::max(worst_held, s.held_ms);
  }
  std::printf(
    "  pose lookups that failed: %d; scans closer than 20 ms in stamp: %d; "
    "scans that moved less than 0.1 m: %d\n", untransformable, too_close, too_short);
  std::printf("  worst delivery gap %.1f ms, worst stamp gap %.1f ms\n",
    worst_held, worst_gap);

  rclcpp::shutdown();
  return 0;
}
