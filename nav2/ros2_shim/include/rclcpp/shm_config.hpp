#ifndef RCLCPP__SHM_CONFIG_HPP_
#define RCLCPP__SHM_CONFIG_HPP_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace rclcpp
{
namespace shim
{

inline uint64_t env_u64(const char * name, uint64_t fallback)
{
  const char * value = std::getenv(name);
  if (!value || !*value) {
    return fallback;
  }
  return std::strtoull(value, nullptr, 10);
}

inline std::string env_string(const char * name, const std::string & fallback)
{
  const char * value = std::getenv(name);
  return (value && *value) ? std::string(value) : fallback;
}

inline uint32_t default_slot_count()
{
  static const uint32_t count = static_cast<uint32_t>(env_u64("NAV2_SHM_SLOT_COUNT", 8));
  return count;
}

inline uint32_t default_slot_size()
{
  static const uint32_t size = static_cast<uint32_t>(env_u64("NAV2_SHM_SLOT_SIZE", 2ull << 20));
  return size;
}

// The slot size on a topic that carries a grid, times `default_slot_count()`.
//
// This is what a `/map` or `/costmap` channel costs every process that opens
// it, and it is a commitment rather than a ceiling that grows on demand: a
// pagefile-backed section is committed at its full size, so a channel sized for
// a map nobody sends is memory nothing else can use. 64 MB per slot put 512 MB
// behind every such topic and 6 GB into planner_server alone, which is what a
// `std::bad_alloc` inside bt_navigator looked like from the outside.
//
// 8 MB is 36x the largest message in the reference stack - the demo's 480x480
// map is 230 KB - and still fits a 2800x2800 occupancy grid.
// `NAV2_SHM_LARGE_SLOT_SIZE` raises it for a bigger world; publisher and
// subscriber both read it here, so they stay in agreement. A channel that is
// already open keeps the layout it was created with, so the daemon and the nav2
// nodes have to be restarted together after a change.
inline uint32_t large_slot_size()
{
  static const uint32_t size = static_cast<uint32_t>(env_u64("NAV2_SHM_LARGE_SLOT_SIZE", 8ull << 20));
  return size;
}

// A parameter service carries a name list, a handful of values or a descriptor
// - never a grid - and each node has twelve channels of them (six services, a
// request and a response each). At the default slot size those twelve rings
// would commit 192 MB per node process, which is the same trap the large slot
// size below fell into: planner_server alone builds six nodes. 256 KB holds a
// `list_parameters` answer for a node with thousands of parameters, and
// `NAV2_SHM_PARAM_SLOT_SIZE` raises it if one ever does not.
inline uint32_t parameter_slot_size()
{
  static const uint32_t size =
    static_cast<uint32_t>(env_u64("NAV2_SHM_PARAM_SLOT_SIZE", 256ull << 10));
  return size;
}

// Is this one of the six parameter services - the question
// `slot_size_for_topic` answers with `parameter_slot_size()`?
//
// The test is on the end of the name, because the *service* name is what both
// ends size the channel from: the server through `Node::create_service`, the
// bridge's CLI helper through `ros2_bridge::create_service_channel`. Neither
// ever asks about `<service>/_request`, so `/controller_server/get_parameters/
// _request` does not have to be recognised here.
inline bool is_parameter_service_name(const std::string & name)
{
  auto ends_with = [&name](const char * suffix) {
      const std::size_t length = std::char_traits<char>::length(suffix);
      return name.size() > length && name.compare(name.size() - length, length, suffix) == 0;
    };
  return ends_with("/get_parameters") || ends_with("/get_parameter_types") ||
         ends_with("/set_parameters") || ends_with("/set_parameters_atomically") ||
         ends_with("/describe_parameters") || ends_with("/list_parameters");
}

// Is this one of the action status topics - `<action>/_action/status`?
//
// One per action server, and there are twenty of them in the nav2 stack. The
// message is a `GoalStatusArray`: a handful of goals, each a 16-byte uuid, a
// stamp and a status code. At the 2 MB default each of those topics would
// commit the same ring a `/scan` gets, to say where a goal got to.
inline bool is_action_status_name(const std::string & name)
{
  static const std::string suffix = "/_action/status";
  return name.size() > suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Costmaps, occupancy grids and maps can be large, everything else is small.
//
// The test is on the topic *name*, because when the channel is created that is
// all the shim has - but a name is not a type, and
// `/keepout_costmap_filter_info` carries a `CostmapFilterInfo`: a handful of
// numbers with "costmap" in their address. At the large size those two topics
// alone were a gigabyte of ring in each costmap-owning process, so a topic that
// says it is `..._info` gets the small size whatever else its name contains.
inline uint32_t slot_size_for_topic(const std::string & topic)
{
  if (is_parameter_service_name(topic)) {
    return parameter_slot_size();
  }
  // `/parameter_events` is the other parameter-shaped name on the graph, and it
  // is not a service: `is_parameter_service_name` says no, and neither "info"
  // nor "map" is in it, so it would take the 2 MB default. That is
  // `default_slot_count()` times over - 16 MB of committed ring in every process
  // that runs a node - for a stream whose messages are a node name and a handful
  // of name/value pairs. Same size as a parameter service, same reason.
  if (topic == "/parameter_events") {
    return parameter_slot_size();
  }
  // `/rosout` is the third parameter-shaped name - a stamp, a severity, a node
  // name, one line of text and a call site - and it takes the same size for the
  // same reason. At the 2 MB default a node's whole log ring would commit 16 MB
  // to say a handful of words.
  if (topic == "/rosout") {
    return parameter_slot_size();
  }
  // `/navigate_to_pose/_action/status` is the fourth parameter-shaped name:
  // small, frequent, and one per action server rather than one per stack. Same
  // size, same reason.
  if (is_action_status_name(topic)) {
    return parameter_slot_size();
  }
  if (topic.find("info") != std::string::npos) {
    return default_slot_size();
  }
  if (topic.find("costmap") != std::string::npos ||
    topic.find("map") != std::string::npos ||
    topic.find("voxel") != std::string::npos)
  {
    return large_slot_size();
  }
  return default_slot_size();
}

// "<topic>" -> "ros2shim<topic>". One flat name space is what makes the bridge
// daemon's mirror of /scan and a nav2 node's subscription line up without any
// configuration - but it also means every process on the machine shares those
// channels, and a channel has exactly one writer. Two stacks that must not see
// each other (the shim test suite next to a live bridge, two robots) therefore
// put a name in front of it:
//
//     set NAV2_SHM_NAMESPACE=test   ->   "ros2shimtest/scan"
//
// Everything in the same window reads the same value, so the daemon, the nav2
// nodes and the `ros2` CLI stay in sync; a process started without it cannot
// collide with one that was.
inline std::string channel_name(const std::string & topic)
{
  static const std::string prefix = "ros2shim" + env_string("NAV2_SHM_NAMESPACE", "");
  return prefix + topic;
}

}  // namespace shim
}  // namespace rclcpp

#endif  // RCLCPP__SHM_CONFIG_HPP_
