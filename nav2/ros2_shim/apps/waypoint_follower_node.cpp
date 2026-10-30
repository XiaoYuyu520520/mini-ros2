// `waypoint_follower`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_waypoint_follower::WaypointFollower and
// spins; this does the same and adds the parameter plumbing every runner here
// needs (see ros2_shim/server_runner.hpp).
//
// It owns the `FollowWaypoints` action: a list of poses goes in, and the node
// sends one `NavigateToPose` goal per pose through bt_navigator, running each
// waypoint's task executors (`WaitAtWaypoint`, `InputAtWaypoint`) on arrival.
// That is the second of the two navigators in a full nav2 stack - the first
// being bt_navigator itself - and it is what makes a multi-waypoint route
// expressible from the Windows side.
//
// `photo_at_waypoint` is the one task executor upstream ships that needs
// OpenCV, which this toolchain does not have; the package's library was built
// without it (see CMakeLists.txt). `photo_at_waypoint` in a parameters file
// therefore fails to load, and the others are unaffected.
#include "ros2_shim/server_runner.hpp"

#include "nav2_waypoint_follower/waypoint_follower.hpp"

namespace
{

const char * kUsage =
  "usage: waypoint_follower [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'waypoint_follower' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `waypoint_follower` block names\n"
  "the waypoint task executors and the `NavigateToPose` action it drives.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_waypoint_follower::WaypointFollower>(
    argc, argv, "waypoint_follower", "waypoint_follower", kUsage);
}
