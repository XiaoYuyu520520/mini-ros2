// Live probe: compare nav2_amcl's localization against the robot's odometry.
//
// Not a ctest - it needs the bridge daemon up (with /scan, /odom and /tf) and
// an AMCL node on the same shim graph:
//
//     call bridge\windows\bridge_env.bat
//     nav2\ros2_shim\build\amcl_probe.exe --duration 60
//
// `ros2 topic echo` reads one topic at a time, so an /amcl_pose read is
// compared against an /odom read a moment later - at 0.3 m/s that skew alone
// looks like tens of centimetres of "error". This probe subscribes to
// everything at once and keeps a short /odom history, so the AMCL pose can be
// compared both against the newest odometry and against the odometry *at the
// instant AMCL stamped*. That separates "the filter is wrong" from "the
// estimate is stale".
//
// The comparison assumes the map frame and the odom frame coincide, which holds
// for the bundled simulator: its odometry is already in world coordinates and
// map -> odom only corrects the small offset AMCL ends up with. A real robot
// would compose in the map -> odom transform first.
#include "rclcpp/rclcpp.hpp"

#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <thread>

namespace
{

// Not every toolchain exposes kPi in strict C++ mode.
constexpr double kPi = 3.14159265358979323846;

const char * kUsage =
  "usage: amcl_probe [options]\n"
  "  --duration S     how long to watch        (default 60)\n"
  "  --period S       seconds between reports  (default 2)\n"
  "  --history S      odometry history to keep (default 30)\n";

double seconds(const builtin_interfaces::msg::Time & time)
{
  return static_cast<double>(time.sec) + static_cast<double>(time.nanosec) * 1e-9;
}

double yawOf(double z, double w)
{
  return std::atan2(2.0 * w * z, 1.0 - 2.0 * z * z);
}

struct Pose
{
  double stamp = 0.0;
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
};

double distance(const Pose & a, const Pose & b)
{
  return std::hypot(a.x - b.x, a.y - b.y);
}

double headingError(const Pose & a, const Pose & b)
{
  double delta = std::fmod(a.yaw - b.yaw + kPi, 2.0 * kPi);
  if (delta < 0.0) {
    delta += 2.0 * kPi;
  }
  return std::fabs(delta - kPi);
}

}  // namespace

int main(int argc, char ** argv)
{
  double duration = 60.0;
  double period = 2.0;
  double history = 30.0;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&](const char * option) -> const char * {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "[amcl_probe] missing value for %s\n", option);
          std::exit(2);
        }
        return argv[++i];
      };
    if (arg == "--help" || arg == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    } else if (arg == "--duration") {
      duration = std::strtod(next("--duration"), nullptr);
    } else if (arg == "--period") {
      period = std::strtod(next("--period"), nullptr);
    } else if (arg == "--history") {
      history = std::strtod(next("--history"), nullptr);
    }
  }

  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("amcl_probe");

  Pose odom;
  Pose amcl;
  double scan_stamp = 0.0;
  bool have_odom = false;
  bool have_amcl = false;
  int scan_count = 0;
  std::deque<Pose> odom_history;

  auto scan_sub = node->create_subscription<sensor_msgs::msg::LaserScan>(
    "/scan", rclcpp::SensorDataQoS(),
    [&](sensor_msgs::msg::LaserScan::ConstSharedPtr message) {
      scan_stamp = seconds(message->header.stamp);
      ++scan_count;
    });

  auto odom_sub = node->create_subscription<nav_msgs::msg::Odometry>(
    "/odom", rclcpp::QoS(rclcpp::KeepLast(50)),
    [&](nav_msgs::msg::Odometry::ConstSharedPtr message) {
      odom.stamp = seconds(message->header.stamp);
      odom.x = message->pose.pose.position.x;
      odom.y = message->pose.pose.position.y;
      odom.yaw = yawOf(message->pose.pose.orientation.z, message->pose.pose.orientation.w);
      have_odom = true;
      odom_history.push_back(odom);
      while (!odom_history.empty() && odom.stamp - odom_history.front().stamp > history) {
        odom_history.pop_front();
      }
    });

  auto amcl_sub = node->create_subscription<geometry_msgs::msg::PoseWithCovarianceStamped>(
    "/amcl_pose", rclcpp::QoS(rclcpp::KeepLast(10)),
    [&](geometry_msgs::msg::PoseWithCovarianceStamped::ConstSharedPtr message) {
      amcl.stamp = seconds(message->header.stamp);
      amcl.x = message->pose.pose.position.x;
      amcl.y = message->pose.pose.position.y;
      amcl.yaw = yawOf(message->pose.pose.orientation.z, message->pose.pose.orientation.w);
      have_amcl = true;
    });

  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  auto next_report = std::chrono::steady_clock::now();

  double worst_now = 0.0;
  double worst_at_stamp = 0.0;
  double sum_now = 0.0;
  double sum_at_stamp = 0.0;
  int reports = 0;

  while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
    node->spin_once();

    if (std::chrono::steady_clock::now() >= next_report) {
      next_report += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(period));
      if (!have_odom || !have_amcl) {
        std::printf(
          "PROBE waiting: odom=%d amcl=%d scans=%d\n", have_odom ? 1 : 0, have_amcl ? 1 : 0,
          scan_count);
        std::fflush(stdout);
        continue;
      }

      // Odometry as it was when AMCL stamped its estimate.
      const Pose * at_stamp = nullptr;
      double best_delta = history + 1.0;
      for (const auto & sample : odom_history) {
        const double delta = std::fabs(sample.stamp - amcl.stamp);
        if (delta < best_delta) {
          best_delta = delta;
          at_stamp = &sample;
        }
      }

      const double err_now = distance(amcl, odom);
      const double yaw_now = headingError(amcl, odom);
      const double err_then = (at_stamp && best_delta <= 0.5) ? distance(amcl, *at_stamp) : -1.0;
      const double yaw_then = (at_stamp && best_delta <= 0.5) ? headingError(amcl, *at_stamp) : -1.0;
      const double amcl_age = odom.stamp - amcl.stamp;
      const double scan_age = odom.stamp - scan_stamp;

      std::printf(
        "PROBE amcl=(%.3f, %.3f, %6.2f) odom=(%.3f, %.3f, %6.2f) "
        "err_now=%.3f m/%5.1f deg  err_at_amcl_stamp=%.3f m/%5.1f deg  "
        "amcl_age=%.2f s scan_age=%.2f s scans=%d\n",
        amcl.x, amcl.y, amcl.yaw * 180.0 / kPi, odom.x, odom.y, odom.yaw * 180.0 / kPi,
        err_now, yaw_now * 180.0 / kPi, err_then, yaw_then * 180.0 / kPi, amcl_age, scan_age,
        scan_count);
      std::fflush(stdout);

      if (err_then >= 0.0) {
        worst_now = std::max(worst_now, err_now);
        worst_at_stamp = std::max(worst_at_stamp, err_then);
        sum_now += err_now;
        sum_at_stamp += err_then;
        ++reports;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  if (reports > 0) {
    std::printf(
      "PROBE summary reports=%d mean_err_now=%.3f m worst_err_now=%.3f m "
      "mean_err_at_amcl_stamp=%.3f m worst_err_at_amcl_stamp=%.3f m\n",
      reports, sum_now / reports, worst_now, sum_at_stamp / reports, worst_at_stamp);
  } else {
    std::printf("PROBE summary no comparable samples\n");
  }
  std::fflush(stdout);

  rclcpp::shutdown();
  return 0;
}
