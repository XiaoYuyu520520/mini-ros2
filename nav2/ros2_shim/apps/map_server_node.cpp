// `map_server`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_map_server::MapServer and spins; this does
// the same with the parameter plumbing every runner here needs (see
// ros2_shim/server_runner.hpp).
//
// The map comes from `yaml_filename`: a YAML sidecar that names a PGM next to
// it. Loading it is the one place in navigation2 that would otherwise pull in
// GraphicsMagick - map_io.cpp goes through Magick++ - so the port ships a
// PGM-only `Magick++.h` (nav2/ros2_shim/include/Magick++.h) and upstream's file
// compiles unmodified. The `load_map` service loads another file later.
//
// Like the other servers this is a lifecycle node: nothing is published until
// it is configured and activated, by `lifecycle_manager` or by the shim's own
// `/<node>/change_state` services. On activation it publishes `topic_name`
// (default `/map`) with a latched QoS, which is what turns a saved map back
// into the map the costmaps and AMCL localise against.
#include "ros2_shim/server_runner.hpp"

#include "nav2_map_server/map_server.hpp"

namespace
{

const char * kUsage =
  "usage: map_server [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'map_server' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Point it at a map with `-p yaml_filename:=C:\\path\\to\\map.yaml`, or leave it\n"
  "empty and hand it one over the `load_map` service. `-p topic_name:=/map`\n"
  "picks the topic, `-p frame_id:=map` the frame it is published in.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_map_server::MapServer>(
    argc, argv, "map_server", "map_server", kUsage);
}
