#ifndef ROS2_BRIDGE_SERVICE_REGISTRY_HPP
#define ROS2_BRIDGE_SERVICE_REGISTRY_HPP

// The service counterpart of type_registry.hpp.
//
// A service in the shim is a pair of shared-memory channels - `<name>/_request`
// and `<name>/_response`, see nav2/ros2_shim/include/rclcpp/service.hpp - that
// carry a correlation id followed by the shim's own layout of the request or
// the response. A real ROS2 peer on the other end of the gRPC bridge speaks
// CDR instead, and `CallService` is deliberately opaque about the payload, so
// something has to translate. This is that table.
//
// The two halves of a service are ordinary ROS2 messages to the generator
// (`std_srvs/srv/Trigger_Request`, `_Response`), which is why every conversion
// here is the same template pair type_registry.hpp uses for topics - only the
// type names differ.

#include <functional>
#include <string>
#include <vector>

#include "ros2_bridge/type_registry.hpp"

namespace ros2_bridge
{

struct ServiceCodec
{
  // ROS2 service type name, e.g. "std_srvs/srv/Trigger".
  std::string type;
  // The four directions a service payload can travel. The first pair is what
  // serving a *remote* service needs, the second what exposing a local one
  // would need, and between them they are what makes the round trip testable
  // without a network.
  //
  // The request on its way out, towards the remote service.
  std::function<bool(const Bytes & shim, Bytes & cdr)> request_to_cdr;
  // The answer on its way back in.
  std::function<bool(const Bytes & cdr, Bytes & shim)> response_to_shim;
  // A request arriving from a remote caller.
  std::function<bool(const Bytes & cdr, Bytes & shim)> request_to_shim;
  // A local service's answer on its way out.
  std::function<bool(const Bytes & shim, Bytes & cdr)> response_to_cdr;
  // Human readable dump of a shim encoded request/response (used by
  // `ros2 service call`, which prints both).
  std::function<std::string(const Bytes & shim)> describe_request;
  std::function<std::string(const Bytes & shim)> describe_response;
  // Builds a shim encoded request from `field.path=value` assignments, the
  // same shape `ros2 topic pub` takes. Empty when the request has no fields.
  std::function<bool(const std::vector<FieldAssign> &, Bytes &, std::string &)> build_request;
};

// nullptr when the service type is unknown to the bridge.
const ServiceCodec * find_service_codec(const std::string & type_name);

bool is_supported_service(const std::string & type_name);
std::vector<std::string> supported_services();

// The names the two halves of a service are known by on the wire, e.g.
// "std_srvs/srv/Trigger" -> "std_srvs/srv/Trigger_Request".
std::string service_request_type(const std::string & service_type);
std::string service_response_type(const std::string & service_type);

// Normalises the type spellings a user or a peer may produce ("std_srvs/
// Trigger", "std_srvs/srv/Trigger") onto the canonical one.
std::string canonical_service_type(const std::string & type_name);

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_SERVICE_REGISTRY_HPP
