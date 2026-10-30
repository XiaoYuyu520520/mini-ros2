// `lifecycle_manager`, on the shared-memory shim.
//
// The node that makes the other three "run": it holds a
// nav2_util::LifecycleServiceClient per managed node and drives
// `/<node>/change_state` over shared memory, so `autostart: true` brings the
// whole stack from unconfigured to active in one go - exactly what
// `ros2 launch nav2_bringup navigation_launch.py` ends up doing upstream.
//
// Upstream's main() constructs nav2_lifecycle_manager::LifecycleManager and
// spins; this adds the shared parameter plumbing. Note that upstream's
// nav2_params.yaml has no `lifecycle_manager` block (nav2_bringup passes
// `node_names` from the launch file), so `node_names` comes from the port's own
// overlay file, or from a file of your own:
//
//     lifecycle_manager:
//       ros__parameters:
//         autostart: true
//         node_names: ["controller_server", "planner_server", "bt_navigator"]
#include "ros2_shim/server_runner.hpp"

#include "nav2_lifecycle_manager/lifecycle_manager.hpp"

namespace
{

const char * kUsage =
  "usage: lifecycle_manager [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'lifecycle_manager' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "`node_names` is a string array, which only a parameter file can spell:\n"
  "  --params-file <...>/nav2_shim_overrides.yaml\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_lifecycle_manager::LifecycleManager>(
    argc, argv, "lifecycle_manager", "lifecycle_manager", kUsage);
}
