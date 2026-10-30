// `planner_server`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_planner::PlannerServer and spins; this does
// the same and adds the parameter plumbing every runner here needs (see
// ros2_shim/server_runner.hpp). The server is a lifecycle node, so nothing it
// publishes or subscribes to exists until nav2_lifecycle_manager configures and
// activates it over `/<node>/change_state`.
//
// Its global costmap is fed by the `/map` the gRPC bridge mirrors in from
// Ubuntu and by the `/scan`/`/tf` on the same graph, and `/plan` comes out on
// shared memory for `ros2 topic echo /plan` (or for bt_navigator, which asks
// for a path over the ComputePathToPose action).
#include "ros2_shim/server_runner.hpp"

#include "nav2_planner/planner_server.hpp"

namespace
{

const char * kUsage =
  "usage: planner_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'planner_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `planner_server` block names\n"
  "the planner plugin (`GridBased: nav2_navfn_planner::NavfnPlanner` in the\n"
  "shipped file) and the costmap parameters under the `global_costmap` block.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_planner::PlannerServer>(
    argc, argv, "planner_server", "planner_server", kUsage);
}
