#ifndef ROS2_SHIM__SERVER_RUNNER_HPP_
#define ROS2_SHIM__SERVER_RUNNER_HPP_

// Command line and startup for the nav2 server binaries.
//
// Upstream's `planner_server`, `controller_server`, `bt_navigator` and
// `lifecycle_manager` mains only construct the node and spin: parameters arrive
// from a launch file and the lifecycle transitions from
// `nav2_lifecycle_manager`. Here the parameters arrive on the command line in
// the same `-p NAME:=VALUE` / `--params-file FILE` form the other runners take,
// so a server starts straight out of a bat file on upstream's own
// `nav2_params.yaml`:
//
//     planner_server.exe --params-file nav2_params.yaml
//
// The servers keep upstream's behaviour of waiting for external transitions -
// `nav2_lifecycle_manager` really is what configures and activates them - which
// is what the lifecycle services added to the shim's rclcpp_lifecycle exist for.
// A single server can still be driven without a manager, because the shim
// exposes the same `/<node>/change_state` services to any process on the
// machine.

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "ros2_shim/lifecycle_runner.hpp"
#include "ros2_shim/params_file.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace ros2_shim
{

struct ServerArguments
{
  std::vector<rclcpp::Parameter> overrides;
  // The `--params-file` paths, in the order they were given. `overrides` above
  // has this process's own node expanded out of them; the paths travel on in the
  // node's ROS arguments so that the nodes this process creates while it runs -
  // nav2's `local_costmap`, `global_costmap` and their like - can read their own
  // blocks out of the same files (see rclcpp/node.hpp).
  std::vector<std::string> parameter_files;
  // The `-r` rules, already paired with their flag, ready to append to the
  // node's ROS arguments (see `remapArguments`).
  std::vector<std::string> remap_arguments;
  double duration = 0.0;  // seconds; 0 = run until stopped
  bool help = false;
  bool ok = true;         // false when an argument was malformed
};

// `--params-file` is filtered by the node's own name, which is fixed for each
// of these servers (it is the name the parameters file and the lifecycle
// manager both use), so the node name is an argument rather than something
// discovered from `--node-name`.
inline ServerArguments parseServerArguments(
  int argc, char ** argv, const char * program, const std::string & node_name, const char * usage)
{
  ServerArguments result;
  std::vector<ParameterOverrideRequest> requests;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      result.help = true;
      return result;
    }
    if (arg == "--duration") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "[%s] missing value for --duration\n", program);
        result.ok = false;
        return result;
      }
      result.duration = std::strtod(argv[++i], nullptr);
      continue;
    }
    if (arg == "--params-file" && i + 1 < argc) {
      result.parameter_files.push_back(argv[i + 1]);
    }
    if (const int took = takeParameterArgument(arg, &i, argc, argv, program, requests); took != 0) {
      if (took < 0) {
        result.ok = false;
        return result;
      }
      continue;
    }
    std::fprintf(stderr, "[%s] unknown option '%s'\n%s", program, arg.c_str(), usage);
    result.ok = false;
    return result;
  }
  if (!buildParameterOverrides(requests, node_name, "", result.overrides, program)) {
    result.ok = false;
  }
  result.remap_arguments = remapArguments(requests);
  return result;
}

// Builds `ServerT`, prints what it is going to wait for, and spins. The
// `if constexpr` keeps one runner for both kinds of nav2 node: the servers are
// lifecycle nodes, the lifecycle manager itself is not.
template<class ServerT>
int runServer(
  int argc, char ** argv, const char * program, const std::string & node_name, const char * usage)
{
  const ServerArguments arguments = parseServerArguments(argc, argv, program, node_name, usage);
  if (arguments.help) {
    std::fputs(usage, stdout);
    return 0;
  }
  if (!arguments.ok) {
    return 2;
  }

  rclcpp::init(argc, argv);

  // The shim core has no YAML parser of its own; this hands it the one in
  // ros2_shim_params so that every node built from the options below can find
  // its block in the `--params-file` files (see rclcpp/node_options.hpp).
  ros2_shim::installParameterFileLoader();

  // The files go into the node's ROS arguments as well as being expanded into
  // `overrides`: that is where upstream keeps them (`ros2 run pkg node
  // --ros-args --params-file FILE`) and the only place nav2's child nodes
  // inherit, because `getChildNodeOptions()` keeps `arguments()` and drops the
  // rest of the parent's options.
  std::vector<std::string> ros_arguments{"--ros-args"};
  for (const auto & file : arguments.parameter_files) {
    ros_arguments.push_back("--params-file");
    ros_arguments.push_back(file);
  }
  // The `-r` rules go on last, so a rule given after a file is still the rule
  // that resolves names.
  ros_arguments.insert(
    ros_arguments.end(), arguments.remap_arguments.begin(), arguments.remap_arguments.end());
  rclcpp::NodeOptions options;
  options.parameter_overrides(arguments.overrides);
  options.arguments(ros_arguments);
  auto node = std::make_shared<ServerT>(options);
  std::printf(
    "[%s] '%s' started with %zu parameter override(s)\n", program, node_name.c_str(),
    arguments.overrides.size());
  std::fflush(stdout);

  if constexpr (std::is_base_of_v<rclcpp_lifecycle::LifecycleNode, ServerT>) {
    std::printf(
      "[%s] waiting for external lifecycle transitions on /%s/change_state\n",
      program, node_name.c_str());
    std::fflush(stdout);
    spinUntilDone(
      std::static_pointer_cast<rclcpp_lifecycle::LifecycleNode>(node), arguments.duration);
  } else {
    spinNodeUntilDone(node, arguments.duration);
  }
  return 0;
}

}  // namespace ros2_shim

#endif  // ROS2_SHIM__SERVER_RUNNER_HPP_
