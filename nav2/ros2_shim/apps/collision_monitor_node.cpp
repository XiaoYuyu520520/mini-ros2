// `collision_monitor`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_collision_monitor::CollisionMonitor and
// spins; this does the same and adds the parameter plumbing every runner here
// needs (see ros2_shim/server_runner.hpp).
//
// It is the one node in the stack whose job is to say *no*: it watches the
// velocity stream it is asked to forward (`cmd_vel_in_topic`), the sensor data
// named by its `observation_sources` (`scan`, `pointcloud`, `range`, `costmap`
// or `polygon`), and the polygons in its `polygons` list, and it either slows
// the command down (a `approach` / `limit` polygon) or replaces it with zero (a
// `stop` polygon) before writing to `cmd_vel_out_topic`. `controller_server`
// checks collisions along its own path; this checks whatever actually reached
// the wheels against what the sensors currently see.
//
// Nothing here goes through pluginlib - the node builds its polygons and its
// observation sources itself out of the classes in this package - so this
// binary needs no whole-archive entry the way nav2_controller's does.
#include "ros2_shim/server_runner.hpp"

#include "nav2_collision_monitor/collision_monitor_node.hpp"

namespace
{

const char * kUsage =
  "usage: collision_monitor [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'collision_monitor' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `collision_monitor` block names\n"
  "the polygons, the observation sources and the cmd_vel topics it sits between.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_collision_monitor::CollisionMonitor>(
    argc, argv, "collision_monitor", "collision_monitor", kUsage);
}
