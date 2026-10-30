// `behavior_server`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_behaviors' BehaviorServer and spins; this
// does the same and adds the parameter plumbing every runner here needs (see
// ros2_shim/server_runner.hpp).
//
// The server is what the recovery branch of a behavior tree talks to. Without a
// process on this node the BT nodes `Spin`, `Wait` and `BackUp` have no action
// server to send to, and a BT.CPP action node whose server is missing makes
// `bt_navigator` refuse to activate *at all* - not just its recovery branch.
// That is why `config/bt/*.xml` used to carry upstream's recovery round robin
// removed; with this binary running, upstream's tree is the tree that is loaded
// again (see the header of those files).
//
// It hosts behavior plugins (`nav2_behaviors::Spin`, `BackUp`,
// `DriveOnHeading` and `Wait` by default) and gives each of them a
// costmap-based collision checker, fed by the `*_costmap_raw` and
// `*_published_footprint` topics that `controller_server`'s local costmap and
// `planner_server`'s global costmap already publish on this graph.
#include "ros2_shim/server_runner.hpp"

#include "nav2_behaviors/behavior_server.hpp"

namespace
{

const char * kUsage =
  "usage: behavior_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'behavior_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `behavior_server` block names\n"
  "the behavior plugins, and `local_costmap_topic` / `global_costmap_topic` name\n"
  "the costmaps the collision checkers read.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<behavior_server::BehaviorServer>(
    argc, argv, "behavior_server", "behavior_server", kUsage);
}
