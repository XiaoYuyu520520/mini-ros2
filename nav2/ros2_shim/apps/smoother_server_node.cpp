// `smoother_server`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_smoother::SmootherServer and spins; this
// does the same and adds the parameter plumbing every runner here needs (see
// ros2_shim/server_runner.hpp).
//
// The server owns the path smoothers (`SimpleSmoother` and
// `SavitzkyGolaySmoother` in the shipped parameters file) and advertises the
// `smooth_path` action plus the `smoother_selector` topic a behavior tree uses
// to pick between them. Nothing in this port's trees calls it yet - the trees
// are the older pipeline's, which smooths nothing - so it is here for callers
// that ask for it directly: `ros2 action send_goal /smooth_path ...` with the
// `nav2_msgs/action/SmoothPath` the shim already generates.
#include "ros2_shim/server_runner.hpp"

#include "nav2_smoother/nav2_smoother.hpp"

namespace
{

const char * kUsage =
  "usage: smoother_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'smoother_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `smoother_server` block's\n"
  "`smoother_plugins` list names the smoothers to load.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_smoother::SmootherServer>(
    argc, argv, "smoother_server", "smoother_server", kUsage);
}
