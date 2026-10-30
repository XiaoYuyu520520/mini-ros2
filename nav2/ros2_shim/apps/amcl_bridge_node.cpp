// Runs nav2_amcl against the shared-memory graph on Windows.
//
// Upstream's `amcl` main() only spins: a lifecycle manager drives
// configure/activate over services in a real ROS 2 deployment, and there is no
// manager on this side of the bridge. `ros2_shim::activateNode()` does both
// transitions itself, which is what makes
//
//     amcl_bridge_node.exe -p base_frame_id:=base_link --initial-pose 0,0,0
//
// localize a robot from the /scan, /map and /tf the gRPC bridge mirrors in
// from the Ubuntu side. The particle filter runs entirely in here; the result
// is published on /amcl_pose and as the `map` -> `odom` transform.
//
// AMCL reaches its map through the `map` topic, not through a map server - the
// ported stack has no nav2_map_server of its own to reach for by default, so a
// bridged /map is the usual source; `run_nav2_stack.py --map FILE` is the other
// one, and map_server publishes that map latched on the same topic.
#include "rclcpp/rclcpp.hpp"
#include "ros2_shim/lifecycle_runner.hpp"

#include "nav2_amcl/amcl_node.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{

const char * kUsage =
  "usage: amcl_bridge_node [options]\n"
  "  --initial-pose X,Y[,YAW]        shorthand for set_initial_pose + initial_pose.*\n"
  "  --duration S                    exit after S seconds (0 = until stopped)\n"
  "  -p, --param NAME:=VALUE         parameter override (repeatable)\n"
  "  --params-file FILE              ROS2 parameter file (repeatable)\n"
  "  -r, --remap FROM:=TO            remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args                      ignored; the separator `ros2 run` inserts\n"
  "  --initial-pose-from-odom        start from the pose on /odom (see below)\n"
  "  --help\n"
  "\n"
  "--initial-pose-from-odom reads /odom once and starts the filter there. It\n"
  "assumes odom and map coincide at startup, which holds for the bundled\n"
  "simulator (its odometry is already in world coordinates) and is the usual\n"
  "guess right after a robot is switched on. An explicit --initial-pose wins.\n"
  "\nParameters use the same `name:=value` syntax as `ros2 run ... --ros-args`.\n"
  "`--params-file nav2_params.yaml` reads the `amcl` block of a nav2 config, and\n"
  "`--initial-pose` wins over both it and `-p`.\n"
  "Topic and frame names default to nav2's amcl config file: `scan`, `map`,\n"
  "`map`/`odom`/`base_footprint` - relative names, which the shim resolves\n"
  "against the node namespace into the /scan, /map the bridge mirrors.\n";

// `--initial-pose 1.5,-2,90` (degrees) or `--initial-pose 1.5,-2,1.57`? nav2's
// own initial_pose.yaw is radians, so radians it is; the third value is
// optional and defaults to 0.
bool appendInitialPose(const std::string & spec, std::vector<rclcpp::Parameter> & overrides)
{
  double values[3] = {0.0, 0.0, 0.0};
  size_t field = 0;
  size_t start = 0;
  std::string text = spec;
  while (field < 3) {
    const size_t comma = text.find(',', start);
    const std::string token =
      text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (token.empty()) {
      break;
    }
    char * end = nullptr;
    values[field] = std::strtod(token.c_str(), &end);
    if (end == token.c_str()) {
      std::fprintf(stderr, "[amcl_bridge_node] --initial-pose: '%s' is not a number\n",
        token.c_str());
      return false;
    }
    ++field;
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (field < 2) {
    std::fprintf(stderr, "[amcl_bridge_node] --initial-pose wants X,Y[,YAW], got '%s'\n",
      spec.c_str());
    return false;
  }
  overrides.emplace_back("set_initial_pose", true);
  overrides.emplace_back("initial_pose.x", values[0]);
  overrides.emplace_back("initial_pose.y", values[1]);
  overrides.emplace_back("initial_pose.yaw", values[2]);
  return true;
}

}  // namespace

int main(int argc, char ** argv)
{
  double duration = 0.0;
  bool initial_pose_from_odom = false;
  bool explicit_pose = false;
  std::vector<ros2_shim::ParameterOverrideRequest> requests;
  // `--initial-pose` is the most specific way to say where the robot starts, so
  // it is collected separately and applied on top of `-p` and `--params-file`.
  std::vector<rclcpp::Parameter> pose_overrides;
  std::vector<rclcpp::Parameter> overrides;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&](const char * option) -> const char * {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "[amcl_bridge_node] missing value for %s\n", option);
          std::exit(2);
        }
        return argv[++i];
      };

    if (arg == "--help" || arg == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    } else if (arg == "--duration") {
      duration = std::strtod(next("--duration"), nullptr);
    } else if (arg == "--initial-pose") {
      if (!appendInitialPose(next("--initial-pose"), pose_overrides)) {
        return 2;
      }
      explicit_pose = true;
    } else if (arg == "--initial-pose-from-odom") {
      initial_pose_from_odom = true;
    } else if (const int took = ros2_shim::takeParameterArgument(
        arg, &i, argc, argv, "amcl_bridge_node", requests); took != 0)
    {
      if (took < 0) {
        return 2;
      }
    } else {
      std::fprintf(stderr, "[amcl_bridge_node] unknown option '%s'\n%s", arg.c_str(), kUsage);
      return 2;
    }
  }

  // A parameters file is keyed on the node's own name, and nav2_amcl registers
  // as `amcl` (nav2_amcl/src/amcl_node.cpp), which is the block a nav2
  // parameters file - and `--params-file` - uses.
  if (!ros2_shim::buildParameterOverrides(requests, "amcl", "", overrides, "amcl_bridge_node")) {
    return 2;
  }
  overrides.insert(overrides.end(), pose_overrides.begin(), pose_overrides.end());

  rclcpp::init(argc, argv);

  // AMCL has to be told where the robot starts. Seeding that from the
  // odometry the bridge mirrors in is what lets amcl_demo.bat be a single
  // command; `--initial-pose` still wins when both are given.
  if (initial_pose_from_odom && !explicit_pose) {
    double x = 0.0;
    double y = 0.0;
    double yaw = 0.0;
    if (ros2_shim::waitForOdomPose(15.0, &x, &y, &yaw)) {
      std::printf("[amcl_bridge_node] initial pose from /odom: %.3f %.3f %.4f\n", x, y, yaw);
      overrides.emplace_back("set_initial_pose", true);
      overrides.emplace_back("initial_pose.x", x);
      overrides.emplace_back("initial_pose.y", y);
      overrides.emplace_back("initial_pose.yaw", yaw);
    } else {
      std::fprintf(stderr,
        "[amcl_bridge_node] no /odom within 15 s; keeping the default initial pose\n");
    }
    std::fflush(stdout);
  }

  // /tf_static is latched and only lands in shared memory after the bridge
  // daemon has carried it across; activating sooner makes the first scans fail
  // their transform lookup. See waitForScanStaticTransform().
  {
    std::string scan_frame;
    if (ros2_shim::waitForScanStaticTransform(15.0, &scan_frame)) {
      std::printf("[amcl_bridge_node] /tf_static carries the %s frame\n",
        scan_frame.c_str());
    } else {
      std::fprintf(stderr,
        "[amcl_bridge_node] no static transform for the scan frame after 15 s;"
        " scans are dropped until /tf_static arrives\n");
    }
    std::fflush(stdout);
  }

  rclcpp::NodeOptions options;
  options.parameter_overrides(overrides);
  // `-r` rules travel on the node's ROS arguments rather than as parameters,
  // which is where `Node::resolve_topic` reads them from (rclcpp/node.hpp).
  options.arguments(ros2_shim::remapArguments(requests));

  auto node = std::make_shared<nav2_amcl::AmclNode>(options);

  if (!ros2_shim::activateNode(node, "amcl_bridge_node")) {
    return 1;
  }
  std::printf("[amcl_bridge_node] active: %s + %s, %s -> %s -> %s\n",
    node->get_parameter("scan_topic").as_string().c_str(),
    node->get_parameter("map_topic").as_string().c_str(),
    node->get_parameter("global_frame_id").as_string().c_str(),
    node->get_parameter("odom_frame_id").as_string().c_str(),
    node->get_parameter("base_frame_id").as_string().c_str());
  std::printf("[amcl_bridge_node] %s motion model, %s laser model, %d..%d particles\n",
    node->get_parameter("robot_model_type").as_string().c_str(),
    node->get_parameter("laser_model_type").as_string().c_str(),
    static_cast<int>(node->get_parameter("min_particles").as_int()),
    static_cast<int>(node->get_parameter("max_particles").as_int()));
  std::fflush(stdout);

  ros2_shim::spinUntilDone(node, duration);
  return 0;
}
