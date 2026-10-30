// `costmap_filter_info_server`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_map_server::CostmapFilterInfoServer and
// spins; this does the same and adds the parameter plumbing every runner here
// needs (see ros2_shim/server_runner.hpp).
//
// It publishes the metadata a costmap filter needs to find its mask (type,
// resolution, origin, the filter's own topic). Both filters in the shipped
// parameters file are off - the port has no keepout or speed mask server
// feeding them - so this node is here for the same reason the smoother server
// is: the piece exists, is startable, and is what a caller looking for it
// expects to find on the graph.
#include "ros2_shim/server_runner.hpp"

#include "nav2_map_server/costmap_filter_info_server.hpp"

namespace
{

const char * kUsage =
  "usage: costmap_filter_info_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'costmap_filter_info_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_map_server::CostmapFilterInfoServer>(
    argc, argv, "costmap_filter_info_server", "costmap_filter_info_server", kUsage);
}
