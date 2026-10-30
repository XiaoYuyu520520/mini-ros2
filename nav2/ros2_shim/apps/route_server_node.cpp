// `route_server`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_route::RouteServer and spins; this does the
// same and adds the parameter plumbing every runner here needs (see
// ros2_shim/server_runner.hpp).
//
// Route is nav2's graph-based planner: instead of a costmap and a search it
// reads a graph of nodes and edges and answers `ComputeRoute` /
// `ComputeAndTrackRoute`. The graph comes out of `graph_filepath`, and the
// edge scorers, route operations and file loaders are all plugins - which is
// why this library is whole-archived on its nav2_server line rather than merely
// linked.
//
// Like docking, this is the compile-and-load half: no graph file ships with
// this port, so the server comes up and waits for one rather than routing
// anything.
#include "ros2_shim/server_runner.hpp"

#include "nav2_route/route_server.hpp"

namespace
{

const char * kUsage =
  "usage: route_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'route_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `route_server` block lists\n"
  "the graph file, the edge scorers and the route operations to load.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_route::RouteServer>(
    argc, argv, "route_server", "route_server", kUsage);
}
