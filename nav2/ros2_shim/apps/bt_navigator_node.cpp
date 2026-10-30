// `bt_navigator`, on the shared-memory shim.
//
// Upstream's main() plus the shared parameter plumbing. This is the node that
// turns a NavigateToPose goal into a behaviour tree: it loads
// `default_nav_to_pose_bt_xml`, whose nodes call the planner and controller
// servers' actions.
//
// The XML the upstream parameters point at is looked up through
// `ament_index_cpp::get_package_share_directory("nav2_bt_navigator")`, which on
// this side of the bridge resolves under `NAV2_SHIM_PREFIX` - so the behavior
// trees have to be under
//   %NAV2_SHIM_PREFIX%\share\nav2_bt_navigator\behavior_trees\
// for the default to be found. `-p default_nav_to_pose_bt_xml:=<absolute path>`
// overrides that for a one-off run.
#include "ros2_shim/server_runner.hpp"

#include "nav2_bt_navigator/bt_navigator.hpp"

namespace
{

const char * kUsage =
  "usage: bt_navigator [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'bt_navigator' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_bt_navigator::BtNavigator>(
    argc, argv, "bt_navigator", "bt_navigator", kUsage);
}
