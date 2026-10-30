// `controller_server`, on the shared-memory shim.
//
// Same shape as planner_server_node.cpp: upstream's main() plus the shared
// parameter plumbing. The controller plugin is whichever one the parameters
// name under `<name>.plugin` - upstream's nav2_params.yaml ships MPPI, which is
// not ported, so the Windows side selects
// nav2_regulated_pure_pursuit_controller::RegulatedPurePursuitController from
// its own overlay file (nav2/ros2_shim/config/nav2_shim_overrides.yaml).
//
// The local costmap is built from the bridged `/scan`, and the velocities the
// controller produces go out on `/cmd_vel`, which the bridge daemon forwards to
// the Ubuntu side.
#include "ros2_shim/server_runner.hpp"

#include "nav2_controller/controller_server.hpp"

namespace
{

const char * kUsage =
  "usage: controller_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'controller_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "The controller, progress checker and goal checker plugins all come from the\n"
  "parameters, e.g.\n"
  "  -p FollowPath.plugin:=nav2_regulated_pure_pursuit_controller::RegulatedPurePursuitController\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_controller::ControllerServer>(
    argc, argv, "controller_server", "controller_server", kUsage);
}
