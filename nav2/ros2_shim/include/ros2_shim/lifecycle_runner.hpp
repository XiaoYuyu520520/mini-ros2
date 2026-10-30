#ifndef ROS2_SHIM__LIFECYCLE_RUNNER_HPP_
#define ROS2_SHIM__LIFECYCLE_RUNNER_HPP_

// Shared plumbing for the Windows-side node runners (slam_bridge_node,
// amcl_bridge_node, ...).
//
// Upstream's node binaries only spin: in a normal ROS 2 deployment a lifecycle
// manager drives configure/activate over services, and nothing on this side of
// the bridge does that. The runners therefore make both transitions themselves
// and then spin, which is what `activateAndSpin()` below is for. It also
// carries the `-p NAME:=VALUE` parsing every runner needs, so a parameter
// override means the same thing in all of them.

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "ros2_shim/parameter_text.hpp"
#include "ros2_shim/params_file.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "tf2_msgs/msg/tf_message.hpp"
#include "lifecycle_msgs/msg/state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ros2_shim
{

// Splits `NAME:=VALUE`, the syntax `ros2 run ... --ros-args -p` uses, and reads
// the value with `parseParameterText` - so the one shape a value can have
// besides a bare word, `[a, b, c]`, means the same thing here as it does in
// `ros2 param set`. It did not, and that difference is not cosmetic:
// `slam_toolbox` reads its start pose out of `map_start_pose`, so
//
//     slam_bridge_node.exe --mode localization -p map_start_pose:=[0.0,0.0,0.0]
//
// arrived as the *string* `[0.0,0.0,0.0]`, was dropped as unreadable, and left
// the node logging that no start pose had been given - with the map silently
// never loaded (see bridge/docs/03-validation.md, evidence 47).
inline bool parseParameterSpec(
  const std::string & spec, rclcpp::Parameter & parameter, const char * program)
{
  const size_t sep = spec.find(":=");
  if (sep == std::string::npos || sep == 0) {
    std::fprintf(
      stderr, "[%s] --param wants NAME:=VALUE, got '%s'\n", program, spec.c_str());
    return false;
  }
  std::string error;
  const rclcpp::ParameterValue value = parseParameterText(spec.substr(sep + 2), &error);
  if (value.is_empty()) {
    std::fprintf(stderr, "[%s] %s: %s\n", program, spec.c_str(), error.c_str());
    return false;
  }
  parameter = rclcpp::Parameter(spec.substr(0, sep), value);
  return true;
}

// ---------------------------------------------------------------------------
// Parameters from the command line
//
// `-p NAME:=VALUE` is what the runners have always taken; `--params-file FILE`
// is what upstream `ros2 run ... --ros-args` adds on top of it. Both are
// collected into an ordered list first and expanded afterwards, because a
// parameter file is filtered by node name and `--node-name` may only be read
// further along the command line than the `--params-file` that needs it.
// ---------------------------------------------------------------------------

struct ParameterOverrideRequest
{
  bool from_file = false;
  // `-r FROM:=TO`. It is collected here because it arrives in the same
  // `--ros-args` block as the parameters, but it is not one: it does not go
  // into `overrides`, it goes into the node's ROS arguments, where
  // `NodeOptions` keeps it for the node and for every child node built from
  // the same options (see rclcpp/node.hpp).
  bool is_remap = false;
  std::string text;   // NAME:=VALUE, the path of a parameter file, or FROM:=TO
};

// Consumes one parameter argument, moving `*index` past the value it takes.
//   1  `arg` was a parameter argument and has been appended to `requests`,
//   0  `arg` is something else and the caller keeps looking,
//  -1  `arg` was malformed; a message has been printed and the caller should
//      exit with a usage error.
inline int takeParameterArgument(
  const std::string & arg, int * index, int argc, char ** argv, const char * program,
  std::vector<ParameterOverrideRequest> & requests)
{
  // `ros2 run pkg node --ros-args -p a:=b` leaves the separator on the command
  // line; it only marks where the ROS arguments start, so it is skipped.
  if (arg == "--ros-args") {
    return 1;
  }
  const bool is_spec = arg == "-p" || arg == "--param";
  const bool is_file = arg == "--params-file";
  const bool is_remap = arg == "-r" || arg == "--remap";
  if (!is_spec && !is_file && !is_remap) {
    return 0;
  }
  if (*index + 1 >= argc) {
    std::fprintf(stderr, "[%s] missing value for %s\n", program, arg.c_str());
    return -1;
  }
  const std::string value = argv[++*index];
  if ((is_spec || is_remap) && value.find(":=") == std::string::npos) {
    std::fprintf(
      stderr, "[%s] %s wants %s, got '%s'\n", program, arg.c_str(),
      is_remap ? "FROM:=TO" : "NAME:=VALUE", value.c_str());
    return -1;
  }
  ParameterOverrideRequest request;
  request.from_file = is_file;
  request.is_remap = is_remap;
  request.text = value;
  requests.push_back(std::move(request));
  return 1;
}

// Expands the requests in the order they appeared on the command line: files
// are filtered to `node_name`/`node_namespace`, `NAME:=VALUE` entries are
// parsed, and both append to `overrides`. The order is what makes a later
// argument win, which is how rclcpp reads a command line that mixes `-p` with
// `--params-file`.
inline bool buildParameterOverrides(
  const std::vector<ParameterOverrideRequest> & requests, const std::string & node_name,
  const std::string & node_namespace, std::vector<rclcpp::Parameter> & overrides,
  const char * program)
{
  bool ok = true;
  for (const auto & request : requests) {
    if (request.is_remap) {
      continue;   // not a parameter; `remapArguments` hands it to the node
    }
    if (request.from_file) {
      const size_t before = overrides.size();
      std::string error;
      if (!loadParamsFile(request.text, node_name, node_namespace, overrides, &error)) {
        std::fprintf(stderr, "[%s] %s\n", program, error.c_str());
        ok = false;
        continue;
      }
      std::printf(
        "[%s] %s: %zu parameter(s) for node '%s'\n", program, request.text.c_str(),
        overrides.size() - before, node_name.c_str());
      std::fflush(stdout);
      continue;
    }
    rclcpp::Parameter parameter;
    if (!parseParameterSpec(request.text, parameter, program)) {
      ok = false;
      continue;
    }
    overrides.push_back(parameter);
  }
  return ok;
}

// The `-r` rules out of the parsed command line, in the order they were given
// and in the `{"-r", "from:=to", ...}` form `NodeOptions::arguments()` takes -
// the same place `--params-file` goes, and the only place nav2's child nodes
// look, because `getChildNodeOptions()` keeps `arguments()` and drops the rest.
//
//     velocity_smoother --ros-args -r cmd_vel:=cmd_vel_nav
//
// is then the port's spelling of upstream's
// `Node(..., remappings=[('cmd_vel', 'cmd_vel_nav')])`.
inline std::vector<std::string> remapArguments(
  const std::vector<ParameterOverrideRequest> & requests)
{
  std::vector<std::string> arguments;
  for (const auto & request : requests) {
    if (request.is_remap) {
      arguments.push_back("-r");
      arguments.push_back(request.text);
    }
  }
  return arguments;
}

// One-shot read of `/odom`, for nodes that want to start from wherever the
// robot already is. `/odom` is expressed in the odom frame, so using it as an
// initial guess assumes odom and map coincide at startup - true for the bundled
// simulator, whose odometry is already in world coordinates, and a reasonable
// guess for a robot that was just switched on. Returns false when nothing
// arrived within `timeout` seconds, so the caller can fall back.
inline bool waitForOdomPose(double timeout, double * x, double * y, double * yaw)
{
  auto node = std::make_shared<rclcpp::Node>("odom_initial_pose_probe");
  bool have = false;
  double px = 0.0;
  double py = 0.0;
  double pz = 0.0;
  double pw = 1.0;
  auto subscription = node->create_subscription<nav_msgs::msg::Odometry>(
    "/odom", rclcpp::QoS(rclcpp::KeepLast(10)),
    [&](const nav_msgs::msg::Odometry & message) {
      px = message.pose.pose.position.x;
      py = message.pose.pose.position.y;
      pz = message.pose.pose.orientation.z;
      pw = message.pose.pose.orientation.w;
      have = true;
    });
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(timeout));
  while (rclcpp::ok() && !have && std::chrono::steady_clock::now() < deadline) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!have) {
    return false;
  }
  *x = px;
  *y = py;
  *yaw = std::atan2(2.0 * pw * pz, 1.0 - 2.0 * pz * pz);
  return true;
}

// AMCL and slam_toolbox look the lidar frame up against `base_frame` for every
// scan they process, and both drop a scan whose transform is missing: AMCL logs
// "Couldn't transform from laser to base_link" and slam_toolbox logs
// "discarding scan". The static half of that chain travels on the latched
// /tf_static topic, which only appears in shared memory once the bridge daemon
// has carried the first sample across, so a node that activates right after
// `ros2_bridge.bat start` loses the first couple of seconds of scans. Waiting
// for the frame the scans actually carry to show up in /tf_static closes that
// race without hard-coding a lidar frame name. Returns false when nothing
// arrived in time - callers keep going either way.
inline bool waitForScanStaticTransform(double timeout, std::string * frame_out = nullptr)
{
  auto node = std::make_shared<rclcpp::Node>("scan_tf_static_probe");
  std::string scan_frame;
  std::vector<std::string> static_frames;
  auto scan_subscription = node->create_subscription<sensor_msgs::msg::LaserScan>(
    "/scan", rclcpp::QoS(rclcpp::KeepLast(10)),
    [&](const sensor_msgs::msg::LaserScan & message) {
      scan_frame = message.header.frame_id;
    });
  auto static_subscription = node->create_subscription<tf2_msgs::msg::TFMessage>(
    "/tf_static", rclcpp::QoS(rclcpp::KeepLast(10)).transient_local(),
    [&](const tf2_msgs::msg::TFMessage & message) {
      for (const auto & transform : message.transforms) {
        static_frames.push_back(transform.child_frame_id);
      }
    });

  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(timeout));
  bool ready = false;
  while (rclcpp::ok() && !ready && std::chrono::steady_clock::now() < deadline) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ready = !scan_frame.empty() &&
      std::find(static_frames.begin(), static_frames.end(), scan_frame) != static_frames.end();
  }
  if (ready && frame_out != nullptr) {
    *frame_out = scan_frame;
  }
  return ready;
}

// The two transitions a lifecycle manager would drive upstream. Subscriptions
// and publishers of the ported nodes are created in on_configure(), so nothing
// is visible on the graph until this runs.
inline bool activateNode(
  const std::shared_ptr<rclcpp_lifecycle::LifecycleNode> & node, const char * program)
{
  const auto configured = node->configure();
  if (configured.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
    std::fprintf(
      stderr, "[%s] configure() failed, state=%s\n", program, configured.label().c_str());
    return false;
  }
  const auto activated = node->activate();
  if (activated.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) {
    std::fprintf(
      stderr, "[%s] activate() failed, state=%s\n", program, activated.label().c_str());
    return false;
  }
  return true;
}

// Spins until the process is stopped, or for `duration` seconds when it is
// positive - which is what lets the runners be used from a script.
inline void spinUntilDone(
  const std::shared_ptr<rclcpp_lifecycle::LifecycleNode> & node, double duration)
{
  if (duration <= 0.0) {
    rclcpp::spin(node->get_node_base_interface());
    rclcpp::shutdown();
    return;
  }
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  rclcpp::shutdown();
}

// The same, for a node that is not a lifecycle node - nav2_lifecycle_manager is
// a plain rclcpp::Node, only the servers it manages are lifecycle nodes.
inline void spinNodeUntilDone(const std::shared_ptr<rclcpp::Node> & node, double duration)
{
  if (duration <= 0.0) {
    rclcpp::spin(node->get_node_base_interface());
    rclcpp::shutdown();
    return;
  }
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
    node->spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  rclcpp::shutdown();
}

}  // namespace ros2_shim

#endif  // ROS2_SHIM__LIFECYCLE_RUNNER_HPP_
