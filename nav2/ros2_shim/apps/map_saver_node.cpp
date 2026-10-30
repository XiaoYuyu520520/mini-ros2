// `map_saver`, on the shared-memory shim.
//
// Upstream's main() constructs nav2_map_server::MapSaver and spins. The node
// offers `save_map`: it subscribes to the map topic named in the request, waits
// for one message, and writes it out as a PGM plus a YAML sidecar - the same
// pair `map_server` reads back, so this is what closes
// "map in shared memory -> a file on disk -> a map again".
//
// The writer is the port's own PGM encoder (include/Magick++.h), and the format
// defaults to `pgm` when the request does not name one, which is also the only
// one this build has a coder for.
#include "ros2_shim/server_runner.hpp"

#include "nav2_map_server/map_saver.hpp"

namespace
{

const char * kUsage =
  "usage: map_saver [options]\n"
  "  --params-file FILE   ROS2 parameter file, filtered to the 'map_saver' block\n"
  "  -p, --param NAME:=VALUE   parameter override (repeatable, applied after files)\n"
  "  --duration S         exit after S seconds (0 = until stopped)\n"
  "  -r, --remap FROM:=TO  remap a name (repeatable), e.g. -r cmd_vel:=cmd_vel_nav\n"
  "  --ros-args           ignored; the separator `ros2 run` inserts\n"
  "  -h, --help\n"
  "\n"
  "Saving is a service call, not a start-up option:\n"
  "\n"
  "  ros2 service call /map_saver/save_map nav2_msgs/srv/SaveMap \\\n"
  "    \"{map_topic: /map, map_url: C:/maps/office, image_format: pgm, map_mode: trinary}\"\n"
  "\n"
  "`map_url` is the path without the extension; the PGM and the YAML are named\n"
  "after it. The node has to be active - `lifecycle_manager`, or the shim's own\n"
  "`/<node>/change_state` services - before the service answers.\n";

}  // namespace

int main(int argc, char ** argv)
{
  return ros2_shim::runServer<nav2_map_server::MapSaver>(
    argc, argv, "map_saver", "map_saver", kUsage);
}
