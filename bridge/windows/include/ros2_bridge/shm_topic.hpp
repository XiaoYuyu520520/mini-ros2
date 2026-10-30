#ifndef ROS2_BRIDGE_SHM_TOPIC_HPP
#define ROS2_BRIDGE_SHM_TOPIC_HPP

// Helpers for talking to the Windows ROS2 shim's shared-memory topics from
// outside a node (the bridge daemon and the `ros2` CLI).
//
// Channel naming and slot sizing are taken from the shim itself
// (rclcpp/shm_config.hpp) so anything this header creates is byte compatible
// with what nav2 nodes open.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/common/types.hpp"
#include "rclcpp/shm_config.hpp"
#include "shm_transport/shm_channel.hpp"

namespace ros2_bridge
{

using Bytes = ros2_grpc_sdk::Bytes;

// "<topic>" -> "ros2shim<topic>", the name the shim uses in shared memory.
inline std::string channel_name(const std::string & topic)
{
  return rclcpp::shim::channel_name(topic);
}

inline std::uint32_t slot_size_for(const std::string & topic)
{
  return rclcpp::shim::slot_size_for_topic(topic);
}

inline std::uint32_t slot_count()
{
  return rclcpp::shim::default_slot_count();
}

// Creates (or opens, when a node already created it) the channel for a topic.
inline std::shared_ptr<shm_transport::ShmChannel> create_channel(
  const std::string & topic, std::string * error = nullptr)
{
  return shm_transport::ShmChannel::create(
    channel_name(topic), slot_count(), slot_size_for(topic), error);
}

// Opens an existing channel, e.g. to consume what a local publisher writes.
// Returns nullptr while nobody has created the channel yet.
inline std::shared_ptr<shm_transport::ShmChannel> open_channel(
  const std::string & topic, std::string * error = nullptr)
{
  return shm_transport::ShmChannel::open(channel_name(topic), error);
}

// The two halves of a service: `rclcpp::Node::create_service` and
// `create_client` agree on `<name>/_request` for the calls and
// `<name>/_response` for the answers (see ros2_shim/include/rclcpp/
// client.hpp), and size the slots from the *service* name rather than from the
// channel path - doing it the other way around would disagree with a node for
// a name like `/map_saver/save_map`.
inline std::string service_channel(const std::string & service, const char * role)
{
  return service + "/_" + role;
}

// Creates (or opens, when a node already created it) one half of a service.
inline std::shared_ptr<shm_transport::ShmChannel> create_service_channel(
  const std::string & service, const char * role, std::string * error = nullptr)
{
  return shm_transport::ShmChannel::create(
    channel_name(service_channel(service, role)), slot_count(), slot_size_for(service), error);
}

// Opens one, creating it when nobody has yet - the shim's own rule for a
// channel two processes may both be first to touch.
inline std::shared_ptr<shm_transport::ShmChannel> open_or_create_service_channel(
  const std::string & service, const char * role, std::string * error = nullptr)
{
  std::string open_error;
  auto channel =
    shm_transport::ShmChannel::open(channel_name(service_channel(service, role)), &open_error);
  if (channel) {
    return channel;
  }
  return create_service_channel(service, role, error);
}

// The six channels of an action, spelled the way
// nav2/ros2_shim/include/rclcpp_action/rclcpp_action.hpp spells them
// (`name_ + "/_goal"` and friends):
//
//   <name>/_goal        client -> server   goal id + goal
//   <name>/_goal_ack    server -> client   goal id + accepted byte
//   <name>/_cancel      client -> server   goal id
//   <name>/_cancel_ack  server -> client   goal id + accepted byte
//   <name>/_feedback    server -> client   goal id + feedback
//   <name>/_result      server -> client   goal id + result code + result
//
// A bridged action is reached by exactly the same route a local server is:
// the daemon owns these six channels for an action the robot serves and plays
// the server role on them, so a caller on this machine cannot tell the two
// apart. Note the part names are used with a leading underscore *inside* the
// name, not as a separate path segment (that is services, `/_request`).
inline std::string action_channel(const std::string & action, const char * part)
{
  return action + "/_" + part;
}

// Created rather than opened, the way the daemon creates a mirrored service's
// channels: a `Local\` mapping disappears with its last handle, so whoever is
// advertised as the remote end of an action has to hold on to it.
inline std::shared_ptr<shm_transport::ShmChannel> create_action_channel(
  const std::string & action, const char * part, std::string * error = nullptr)
{
  // Sized from the channel path, which is what `rclcpp_action::Client` and
  // `Server` hand to `Node::get_channel` (`slot_size_for_topic` is substring
  // based, so the distinction only matters for a name containing "map" or
  // "costmap" - but it is the same string either way, and matching exactly is
  // cheaper than explaining why it does not have to).
  return shm_transport::ShmChannel::create(
    channel_name(action_channel(action, part)), slot_count(),
    slot_size_for(action_channel(action, part)), error);
}

// Opens one, creating it when nobody has yet - the shim's own rule for a
// channel two processes may both be first to touch.
inline std::shared_ptr<shm_transport::ShmChannel> open_or_create_action_channel(
  const std::string & action, const char * part, std::string * error = nullptr)
{
  std::string open_error;
  auto channel =
    shm_transport::ShmChannel::open(channel_name(action_channel(action, part)), &open_error);
  if (channel) {
    return channel;
  }
  return create_action_channel(action, part, error);
}

// Drains every message currently buffered on a channel (non blocking).
inline std::size_t drain_channel(
  shm_transport::ShmChannel & channel, std::vector<Bytes> & out,
  std::size_t max_messages = 1024)
{
  out.clear();
  // A slot can hold anything up to the channel's slot size; a smaller buffer
  // would silently consume and drop oversized messages.
  std::vector<std::uint8_t> buffer(channel.slot_size());
  for (std::size_t i = 0; i < max_messages; ++i) {
    std::size_t size = 0;
    if (!channel.subscribe(buffer.data(), buffer.size(), &size, 0)) {
      break;
    }
    if (size <= buffer.size()) {
      out.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
    }
  }
  return out.size();
}

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_SHM_TOPIC_HPP