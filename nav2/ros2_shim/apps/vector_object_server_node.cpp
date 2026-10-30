// `vector_object_server`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_map_server::VectorObjectServer and spins;
// this does the same and adds the parameter plumbing every runner here needs
// (see ros2_shim/server_runner.hpp).
//
// It owns the `vector_object` service family: polygons and circles are
// published as one vector map, kept under a UUID each, and an exclusion zone is
// what a costmap filter reads. Those UUIDs are why the package needs libuuid -
// see include/uuid/uuid.h for the part of it the shim carries - and why it was
// left out of the Windows build until that header existed.
#include "ros2_shim/server_runner.hpp"

#include "nav2_map_server/vector_object_server.hpp"

namespace
{

const char * kUsage =
  "usage: vector_object_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'vector_object_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Reads upstream's nav2_params.yaml directly: the `vector_object_server` block\n"
  "names the shapes to publish and the map frame they are placed in.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_map_server::VectorObjectServer>(
    argc, argv, "vector_object_server", "vector_object_server", kUsage);
}
