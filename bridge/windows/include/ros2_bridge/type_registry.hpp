#ifndef ROS2_BRIDGE_TYPE_REGISTRY_HPP
#define ROS2_BRIDGE_TYPE_REGISTRY_HPP

// Maps the ROS2 type names that travel over the gRPC bridge onto the generated
// C++ message structs of the shared-memory shim, so a message can be converted
// between CDR (on the wire) and the shim's native layout (in shared memory).
//
// Only the types registered in src/type_registry.cpp are understood by the
// bridge; every other topic is passed over with a warning.

#include <functional>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/common/types.hpp"

namespace ros2_bridge
{

using Bytes = ros2_grpc_sdk::Bytes;

// One `field.path=value` assignment given on the command line.
struct FieldAssign
{
  std::string path;
  std::string value;
};

struct Codec
{
  // ROS2 type name, e.g. "sensor_msgs/msg/LaserScan".
  std::string type;
  // shim layout -> CDR (used when publishing towards the remote agent).
  std::function<bool(const Bytes & shim, Bytes & cdr)> to_cdr;
  // CDR -> shim layout (used when mirroring a remote topic into shared memory).
  std::function<bool(const Bytes & cdr, Bytes & shim)> to_shim;
  // Human readable dump of a shim encoded message (used by `ros2 topic echo`).
  std::function<std::string(const Bytes & shim)> describe;
  // Builds a shim encoded message from `field.path=value` assignments
  // (used by `ros2 topic pub`). Empty when the type cannot be built.
  std::function<bool(const std::vector<FieldAssign> &, Bytes &, std::string &)> build;
};

// nullptr when the type is unknown.
const Codec * find_codec(const std::string & type_name);

bool is_supported(const std::string & type_name);
std::vector<std::string> supported_types();

// The helpers below never throw: a malformed payload returns false and leaves
// `out` empty, so a broken peer cannot take the bridge down.
bool shim_to_cdr(const std::string & type_name, const Bytes & shim, Bytes & cdr);
bool cdr_to_shim(const std::string & type_name, const Bytes & cdr, Bytes & shim);
std::string describe_shim(const std::string & type_name, const Bytes & shim);
bool build_shim(
  const std::string & type_name, const std::vector<FieldAssign> & fields, Bytes & shim,
  std::string * error = nullptr);

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_TYPE_REGISTRY_HPP