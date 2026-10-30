#ifndef ROS2_BRIDGE_ACTION_REGISTRY_HPP
#define ROS2_BRIDGE_ACTION_REGISTRY_HPP

// The action counterpart of type_registry.hpp and service_registry.hpp.
//
// An action in the shim is six shared-memory channels, named after the action
// and laid out by nav2/ros2_shim/include/rclcpp_action/rclcpp_action.hpp:
//
//   <name>/_goal        client -> server   8 byte goal id + shim goal
//   <name>/_goal_ack    server -> client   8 byte goal id + 1 byte accepted
//   <name>/_cancel      client -> server   8 byte goal id
//   <name>/_cancel_ack  server -> client   8 byte goal id + 1 byte accepted
//   <name>/_feedback    server -> client   8 byte goal id + shim feedback
//   <name>/_result      server -> client   8 byte goal id + 1 byte result code
//                                          + shim result
//
// A robot on the other end of the gRPC bridge speaks the three payload types
// as real ROS2 actions - goal, feedback and result - so the daemon needs to
// translate each of them between the shim's layout and CDR, and to name the
// result code that goes in front of a result. This is that table.
//
// Only the types registered in src/action_registry.cpp are understood; every
// other action is passed over with a line in the log, like an unknown topic
// type or an unknown service type.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ros2_bridge/type_registry.hpp"
#include "ros2_grpc_sdk/common/types.hpp"

namespace ros2_bridge
{

struct ActionCodec
{
  // ROS2 action type name, e.g. "ros2_bridge_sim/action/Drive".
  std::string type;
  // Its three parts, spelled the way ROS2 names them
  // (".../action/Drive_Goal"). They are what a peer asks for on the wire, and
  // what the description of a goal/feedback/result payload is labelled with.
  std::string goal_type;
  std::string feedback_type;
  std::string result_type;

  // A goal on its way out, towards the robot's action server.
  std::function<bool(const Bytes & shim, Bytes & cdr)> goal_to_cdr;
  // What comes back: feedback while the goal runs, and the result at the end.
  // Both arrive as CDR from a real ROS2 action server and have to land in the
  // shim's layout for the caller's `rclcpp_action::Client` to read them.
  //
  // The other three directions (a goal arriving as CDR, feedback and results
  // leaving as CDR) are what exposing an action *served on this machine* to
  // the robot would need; nothing does that yet, so they are not here - the
  // conversions are the same two templates either way round.
  std::function<bool(const Bytes & cdr, Bytes & shim)> feedback_to_shim;
  std::function<bool(const Bytes & cdr, Bytes & shim)> result_to_shim;

  // The shim encoding of a default-constructed result, i.e. what upstream hands
  // a caller as the result of a goal whose server never filled one in. A goal
  // cancelled before it produced anything is the ordinary case, and the caller
  // still has to be told the goal ended: silence there costs it the whole of
  // its timeout. Used by the daemon when the robot's terminal message carries
  // no payload.
  Bytes default_result;

  // Human readable dumps, used by `ros2 action send_goal` to print what it is
  // about to send and what came back.
  std::function<std::string(const Bytes & shim)> describe_goal;
  std::function<std::string(const Bytes & shim)> describe_feedback;
  std::function<std::string(const Bytes & shim)> describe_result;

  // Builds a shim encoded goal from `field.path=value` assignments, the same
  // shape `ros2 topic pub` and `ros2 service call` take.
  std::function<bool(const std::vector<FieldAssign> &, Bytes &, std::string &)> build_goal;
};

// nullptr when the action type is unknown to the bridge.
const ActionCodec * find_action_codec(const std::string & type_name);

bool is_supported_action(const std::string & type_name);
std::vector<std::string> supported_actions();

// Normalises the type spellings a user or a peer may produce
// ("ros2_bridge_sim/Drive", "ros2_bridge_sim/action/Drive") onto the canonical
// one, the way canonical_service_type does for services.
std::string canonical_action_type(const std::string & type_name);

std::string action_goal_type(const std::string & action_type);
std::string action_feedback_type(const std::string & action_type);
std::string action_result_type(const std::string & action_type);

// The byte in front of a result payload on the shim's `<name>/_result`
// channel, i.e. `rclcpp_action::ResultCode` (nav2/ros2_shim/include/
// rclcpp_action/types.hpp). Spelled out here rather than included so the
// bridge does not have to pull in the whole shim to name four numbers;
// bridge_service_roundtrip's action half checks them against the shim's enum,
// so the two cannot drift apart unnoticed.
enum class ShimResultCode : std::uint8_t
{
  Unknown = 0,
  Succeeded = 1,
  Aborted = 2,
  Canceled = 3,
};

// What a goal that ended with `status` becomes on the shim side.
//
// A rejected goal never gets here: it is refused on the goal-ack channel, the
// way `rclcpp_action::Server` refuses one without a handle. Should a peer send
// one anyway, it is reported as ABORTED rather than quietly reported as a
// success.
ShimResultCode shim_result_code(ros2_grpc_sdk::GoalStatus status);

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_ACTION_REGISTRY_HPP
