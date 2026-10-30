// `docking_server`, on the shared-memory shim.
//
// Upstream's main() constructs opennav_docking::DockingServer and spins; this
// does the same and adds the parameter plumbing every runner here needs (see
// ros2_shim/server_runner.hpp).
//
// The server is a lifecycle node like the rest of the stack, and the manager
// that brings the others up can drive it the same way - `/<node>/change_state`
// is all it needs. What it loads is the dock plugin named in its parameter
// block (`opennav_docking::SimpleChargingDock` or
// `opennav_docking::SimpleNonChargingDock`), which is why the library appears
// in the whole-archive list on the nav2_server line in CMakeLists.txt.
//
// It is here for the compile-and-load half of the docking story, not the docked
// half: no dock in this port's world publishes a `sensor_msgs/BatteryState` or
// a dock pose, so there is nothing yet for it to dock against.
#include "ros2_shim/server_runner.hpp"

#include "opennav_docking/docking_server.hpp"

namespace
{

const char * kUsage =
  "usage: docking_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'docking_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `docking_server` block names\n"
  "the dock plugins to load and the dock database to read.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<opennav_docking::DockingServer>(
    argc, argv, "docking_server", "docking_server", kUsage);
}
