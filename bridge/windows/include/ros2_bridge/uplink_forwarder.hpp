#ifndef ROS2_BRIDGE_UPLINK_FORWARDER_HPP
#define ROS2_BRIDGE_UPLINK_FORWARDER_HPP

// The local-to-remote direction: what this machine publishes becomes a topic
// the robot subscribes to.
//
// The first three directions all start at the agent and end in shared
// memory. This one starts at a shim channel some local process writes - nav2's
// `/cmd_vel`, a `ros2 topic pub`, the demo's `/goal_pose` - and ends in a gRPC
// publish.
//
// The daemon's reader *is* the local subscription for these topics, which
// matters for nav2: the controller only writes `/cmd_vel` when
// `get_subscription_count()` is above zero, and that count comes from the
// shared graph. Without the sub role this service registers, the count is zero
// and the controller stays silent.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ros2_bridge/daemon_options.hpp"
#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "shm_transport/shm_channel.hpp"

namespace ros2_bridge
{

struct UplinkTopic
{
  std::string topic;
  std::string type;
  std::shared_ptr<shm_transport::ShmChannel> channel;
  std::atomic<std::uint64_t> messages{0};
  std::atomic<std::uint64_t> failures{0};
  bool running = false;
  // The channel error is worth printing once, not every 200 ms.
  bool reported = false;
  // The shim graph is only re-read every couple of seconds, and a mismatch is
  // reported once per type pair rather than on every cycle.
  std::chrono::steady_clock::time_point last_type_check;
  std::string warned_type;
  std::thread worker;
};

// What the uplink forwarder needs from the rest of the daemon.
struct UplinkContext
{
  // The one gRPC connection. The daemon owns it; the forwarder only sends on it.
  ros2_grpc_sdk::Ros2BridgeClient * client = nullptr;
  // The agent this daemon talks to, for the log lines.
  const std::string * peer = nullptr;
  // The topics to forward. Empty means the direction is off.
  const std::vector<std::string> * topics = nullptr;
  // Asks the mirror what type the agent advertises for a topic, for a topic
  // this machine does not publish yet (`/cmd_vel` is normally created by
  // nav2 *after* the bridge starts).
  std::function<bool(const std::string &, std::string &)> remote_type_of;
  // The types that were already reported as having no codec, and the topics
  // whose type nobody knows.
  std::shared_ptr<std::set<std::string>> warned_types;
  // Whether the daemon is still running.
  std::function<bool()> running;
};

class UplinkForwarderService
{
public:
  explicit UplinkForwarderService(UplinkContext context);

  // Starts a worker for every uplink that does not have one yet. Idempotent:
  // the daemon calls it on every refresh.
  void sync();

  std::size_t uplink_count() const;

  // The counters, for the periodic report.
  void append_report(std::vector<std::string> & lines) const;

  // Stops the workers and drops the graph records.
  void stop();

private:
  // The type the local publisher of `topic` announced in the shim graph, or
  // empty when nobody publishes it on this machine - the bridge's own record
  // of the topic is `remote:`, so only local records answer this.
  static std::string local_publisher_type(const std::string & topic);
  // Says so when the two ends of an uplinked topic disagree about its type.
  void check_uplink_type(const std::shared_ptr<UplinkTopic> & uplink);
  void uplinkLoop(const std::shared_ptr<UplinkTopic> & uplink);

  UplinkContext context_;
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<UplinkTopic>> uplinks_;
};

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_UPLINK_FORWARDER_HPP
