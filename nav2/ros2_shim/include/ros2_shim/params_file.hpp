#ifndef ROS2_SHIM__PARAMS_FILE_HPP_
#define ROS2_SHIM__PARAMS_FILE_HPP_

#include "rclcpp/parameter.hpp"

#include <string>
#include <vector>

namespace ros2_shim
{

// Reads a ROS2 parameter file - the
//
//     slam_toolbox:
//       ros__parameters:
//         resolution: 0.05
//
// layout that `ros2 run pkg node --ros-args --params-file FILE` reads - and
// appends one rclcpp::Parameter per leaf value that applies to `node_name`.
//
// A top-level block applies when its key names the node (`slam_toolbox`,
// `/slam_toolbox`, `/ns/slam_toolbox`) or when it is a wildcard (`/**`,
// `/ns/**`). Wildcards are applied first so that a block naming the node wins
// over one that matches every node, which is the precedence rclcpp uses; a key
// without a leading slash is read as `/<key>`, again like rclcpp. A block
// without `ros__parameters` underneath it is not a parameter block and is
// skipped.
//
// Every leaf is appended twice - once under its name relative to
// `ros__parameters` (`resolution`, `initial_pose.x`) and once fully qualified
// (`/slam_toolbox.resolution`) - because the shim resolves an override by exact
// name while the ported nodes declare relative names.
//
// Nested maps are flattened with '.', so `initial_pose: {x: 1.0}` yields the
// parameter `initial_pose.x`. Scalars become bool/int64/double/string the way
// rclcpp decides them, so an unquoted `true`, `-1` or `0.05` is not a string;
// sequences become the matching array type. The one place this is laxer than
// rclcpp is quoting: yaml-cpp does not expose whether a scalar was quoted, so
// `"0.05"` reads as a double here.
//
// `node_namespace` is the namespace the node was created in ("" for none).
//
// Returns false when the file cannot be read or parsed, with the reason in
// `*error` when `error` is not null. A file that has no block for this node is
// a success that appends nothing.
bool loadParamsFile(
  const std::string & path, const std::string & node_name,
  const std::string & node_namespace, std::vector<rclcpp::Parameter> & out,
  std::string * error);

// Hands the reader above to rclcpp (`rclcpp::set_parameter_file_loader()`), so
// that a node built from NodeOptions carrying `--params-file` paths can pick its
// own block out of them. Only the runners call this: reading YAML belongs to
// this library, and the shim core - which is header only and linked into
// anything that includes it - must not grow a yaml-cpp dependency to offer
// `--params-file`. Without the call the files are simply ignored by nodes, which
// is what the binaries that take only `-p` overrides want.
void installParameterFileLoader();

}  // namespace ros2_shim

#endif  // ROS2_SHIM__PARAMS_FILE_HPP_
