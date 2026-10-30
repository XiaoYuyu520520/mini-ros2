// `velocity_smoother`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_velocity_smoother::VelocitySmoother and
// spins; this does the same and adds the parameter plumbing every runner here
// needs (see ros2_shim/server_runner.hpp).
//
// It is not a plugin host: the whole node is one class, and what it does is
// reshape the velocity stream between `cmd_vel_in_topic` and
// `cmd_vel_out_topic` - a first-order limit on linear and angular acceleration,
// with an optional deadband, and an optional watchdog that publishes zero when
// the input stops arriving. Upstream's launch files start it between
// `controller_server` and the robot, so what reaches the wheels is inside the
// platform's acceleration envelope even when a controller asks for a step.
//
// Like every other server here it comes up unconfigured and waits for
// `/<node>/change_state`; see nav2_stack.bat for the parameters that wire it
// into the velocity chain.
#include "ros2_shim/server_runner.hpp"

#include "nav2_velocity_smoother/velocity_smoother.hpp"

namespace
{

const char * kUsage =
  "usage: velocity_smoother [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'velocity_smoother' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `velocity_smoother` block names\n"
  "`cmd_vel_in_topic` / `cmd_vel_out_topic` and the acceleration limits.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_velocity_smoother::VelocitySmoother>(
    argc, argv, "velocity_smoother", "velocity_smoother", kUsage);
}
