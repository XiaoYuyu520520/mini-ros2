// Runs a slam_toolbox node against the shared-memory graph on Windows.
//
// Upstream's `slam_toolbox_*_node.cpp` main() only spins and a lifecycle
// manager drives configure/activate from elsewhere; there is none here, so
// `ros2_shim::activateAndSpin()` makes both transitions first. That is what
// makes
//
//     slam_bridge_node.exe -p map_name:=/slam_map
//
// map whatever the shared-memory graph carries - including the /scan, /odom
// and /tf the gRPC bridge mirrors in from the Ubuntu robot.
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "ros2_shim/lifecycle_runner.hpp"
#include "slam_toolbox/slam_toolbox_async.hpp"
#include "slam_toolbox/slam_toolbox_localization.hpp"
#include "slam_toolbox/slam_toolbox_sync.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{

const char * kUsage =
  "usage: slam_bridge_node [options]\n"
  "  --mode sync|async|localization  node flavour      (default sync)\n"
  "  --node-name NAME                node name         (default slam_toolbox)\n"
  "  --namespace NS                  node namespace    (default none)\n"
  "  --duration S                    exit after S seconds (0 = until stopped)\n"
  "  -p, --param NAME:=VALUE         parameter override (repeatable)\n"
  "  --params-file FILE              ROS2 parameter file (repeatable)\n"
  "  -r, --remap FROM:=TO            remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args                      ignored; the separator `ros2 run` inserts\n"
  "  --help\n"
  "\nParameters use the same `name:=value` syntax as `ros2 run ... --ros-args`,\n"
  "so the shipped slam_toolbox config files are used as they are:\n"
  "\n"
  "    slam_bridge_node.exe --params-file mapper_params_online_async.yaml\n"
  "\n"
  "A parameter file is read for the node name `slam_toolbox`, which is the name\n"
  "the ported node registers under. A later argument wins over an earlier one,\n"
  "so `-p` overrides what a preceding `--params-file` set.\n";

// Reads a parameter of the ported node, declaring it first if nobody has.
//
// `map_update_interval` is declared *inside* `publishVisualizations()`, the
// thread `activate()` has just started, and upstream reads it back with a bare
// `get_parameter()` in that same function (`slam_toolbox_common.cpp`), so a
// reader that gets there first declares it itself. The one reader outside that
// thread is the line below, reporting the values a `--params-file` applied - and
// winning that race used to end the process with "parameter
// 'map_update_interval' has not been declared", which reads like a missing
// parameter but is only a missing declaration: the shipped config sets it.
// Declaring is harmless when the thread wins instead - the declaration is what
// carries the file's override in, so both sides read the same value.
template<typename T>
T declared_parameter(
  const std::shared_ptr<rclcpp_lifecycle::LifecycleNode> & node, const std::string & name,
  const T & fallback)
{
  if (!node->has_parameter(name)) {
    node->declare_parameter(name, fallback);
  }
  return node->get_parameter(name).template get_value<T>();
}

}  // namespace

int main(int argc, char ** argv)
{
  std::string mode = "sync";
  std::string node_name = "slam_toolbox";
  std::string node_namespace;
  double duration = 0.0;
  std::vector<ros2_shim::ParameterOverrideRequest> requests;
  std::vector<rclcpp::Parameter> overrides;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&](const char * option) -> const char * {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "[slam_bridge_node] missing value for %s\n", option);
          std::exit(2);
        }
        return argv[++i];
      };

    if (arg == "--help" || arg == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    } else if (arg == "--mode") {
      mode = next("--mode");
    } else if (arg == "--node-name") {
      node_name = next("--node-name");
    } else if (arg == "--namespace") {
      node_namespace = next("--namespace");
    } else if (arg == "--duration") {
      duration = std::strtod(next("--duration"), nullptr);
    } else if (const int took = ros2_shim::takeParameterArgument(
        arg, &i, argc, argv, "slam_bridge_node", requests); took != 0)
    {
      if (took < 0) {
        return 2;
      }
    } else {
      std::fprintf(stderr, "[slam_bridge_node] unknown option '%s'\n%s", arg.c_str(), kUsage);
      return 2;
    }
  }

  // A parameters file is keyed on the node's own name, and the ported node
  // registers as `slam_toolbox` (third_party/slam_toolbox/src/
  // slam_toolbox_common.cpp), so that is the block `--params-file` looks for.
  if (!ros2_shim::buildParameterOverrides(
      requests, "slam_toolbox", "", overrides, "slam_bridge_node"))
  {
    return 2;
  }

  rclcpp::init(argc, argv);
  // /tf_static is latched and only lands in shared memory after the bridge
  // daemon has carried it across; activating sooner makes the first scans fail
  // their transform lookup. See waitForScanStaticTransform().
  {
    std::string scan_frame;
    if (ros2_shim::waitForScanStaticTransform(15.0, &scan_frame)) {
      std::printf("[slam_bridge_node] /tf_static carries the %s frame\n",
        scan_frame.c_str());
    } else {
      std::fprintf(stderr,
        "[slam_bridge_node] no static transform for the scan frame after 15 s;"
        " scans are dropped until /tf_static arrives\n");
    }
    std::fflush(stdout);
  }

  rclcpp::NodeOptions options;
  options.parameter_overrides(overrides);
  // `-r` rules travel on the node's ROS arguments rather than as parameters,
  // which is where `Node::resolve_topic` reads them from (rclcpp/node.hpp).
  options.arguments(ros2_shim::remapArguments(requests));

  // All three flavours take the same options and expose the same
  // configure()/activate(); only the mapping job behind them differs.
  std::shared_ptr<rclcpp_lifecycle::LifecycleNode> node;
  if (mode == "sync") {
    node = std::make_shared<slam_toolbox::SynchronousSlamToolbox>(options);
  } else if (mode == "async") {
    node = std::make_shared<slam_toolbox::AsynchronousSlamToolbox>(options);
  } else if (mode == "localization") {
    node = std::make_shared<slam_toolbox::LocalizationSlamToolbox>(options);
  } else {
    std::fprintf(
      stderr, "[slam_bridge_node] unknown mode '%s' (sync|async|localization)\n", mode.c_str());
    return 2;
  }
  (void)node_name;
  (void)node_namespace;

  if (!ros2_shim::activateNode(node, "slam_bridge_node")) {
    return 1;
  }
  // The values that differ from the node's own defaults are the evidence that a
  // `--params-file` was applied: slam_toolbox defaults `map_update_interval` to
  // 10 s and the shipped config asks for 5 s, and `scan_buffer_size` defaults to
  // 10, which is what mapper_params_online_async.yaml sets as well.
  std::printf("[slam_bridge_node] %s active: %s -> %s, %s frame, resolution %.4f, "
    "map every %.1f s, scan buffer %d, %s scan matching, %s loop closing\n",
    mode.c_str(),
    declared_parameter<std::string>(node, "scan_topic", "/scan").c_str(),
    declared_parameter<std::string>(node, "map_name", "/map").c_str(),
    declared_parameter<std::string>(node, "base_frame", "base_footprint").c_str(),
    declared_parameter<double>(node, "resolution", 0.05),
    declared_parameter<double>(node, "map_update_interval", 10.0),
    static_cast<int>(declared_parameter<int64_t>(node, "scan_buffer_size", 10)),
    declared_parameter<bool>(node, "use_scan_matching", true) ? "with" : "without",
    declared_parameter<bool>(node, "do_loop_closing", true) ? "with" : "without");
  std::fflush(stdout);

  ros2_shim::spinUntilDone(node, duration);
  return 0;
}
