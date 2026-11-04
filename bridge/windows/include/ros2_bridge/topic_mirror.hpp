#ifndef ROS2_BRIDGE_TOPIC_MIRROR_HPP
#define ROS2_BRIDGE_TOPIC_MIRROR_HPP

// The remote-to-local direction: what the agent publishes becomes a topic in
// this machine's shared memory.
//
// This was a quarter of `bridge_daemon.cpp`, and it is the quarter with the
// most state in it: one shared-memory channel per mirrored topic, one gRPC
// subscription per channel, and the bookkeeping that decides when a stream
// has to be rebuilt.
//
// The state that belongs to *this* service is here. What stays with the
// daemon is what crosses the four services: the client (one connection, four
// users), the options, and the graph of the remote topics - which the uplink
// forwarder reads to find the type of a topic it cannot see locally.
//
// The locking follows the data: this object has its own mutex, and the
// daemon's own lock protects only what is left in the daemon. A report that
// walks all four services takes four locks in turn, each for as long as it
// takes to copy that service's counters.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ros2_bridge/daemon_options.hpp"
#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "shm_transport/shm_channel.hpp"

namespace ros2_bridge
{

// One mirrored topic: its channel, its counters and the state of its stream.
struct TopicMirror
{
  std::string topic;
  std::string type;
  std::shared_ptr<shm_transport::ShmChannel> channel;
  std::atomic<std::uint64_t> messages{0};
  std::atomic<std::uint64_t> bytes{0};
  std::atomic<std::uint64_t> failures{0};
  std::atomic<std::int64_t> last_stamp_ns{0};
  // Set by the subscription's close callback, read by the refresh. A gRPC
  // stream is not immortal (the agent restarts, the sim is bounced, the TCP
  // connection is reset), and a mirrored topic whose stream has ended never
  // writes its shared-memory channel again - so this flag is how the refresh
  // loop learns that the mirror has to be rebuilt.
  std::atomic<bool> live{true};
  // How many times this topic had to be re-subscribed over the daemon's
  // lifetime, carried across mirror objects: the graph is otherwise silent
  // about a robot the bridge had to reconnect to.
  std::atomic<std::uint64_t> reconnects{0};
  std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();
};

// Re-subscribing bookkeeping, one entry per topic the agent still advertises.
struct MirrorRetry
{
  int failures = 0;                 // consecutive streams that ended early
  std::uint64_t reconnects = 0;     // successful re-subscribes so far
  std::chrono::steady_clock::time_point next_attempt{};
};

// What the mirror needs from the rest of the daemon.
struct MirrorContext
{
  // The one gRPC connection. The daemon owns it; the mirror only sends on it.
  ros2_grpc_sdk::Ros2BridgeClient * client = nullptr;
  // The agent this daemon talks to, for the graph's "remote:<peer>" source.
  const std::string * peer = nullptr;
  // The topics the uplink forwarder owns. A topic that travels *from* this
  // machine must never be mirrored: a shared-memory channel has one writer,
  // and a mirrored /cmd_vel would feed the daemon's own uplink back to the
  // robot.
  const std::vector<std::string> * uplink_topics = nullptr;
  // The types that were already reported as having no codec, so the warning
  // is printed once per type instead of once per refresh.
  std::shared_ptr<std::set<std::string>> warned_types;
  // Called when the agent cannot be asked for its topics.
  std::function<void(const char *, const std::string &)> report_scan_failure;
  std::function<void(const char *)> clear_scan_failure;
};

// The remote topics the agent advertises, and the mirrors that carry them.
class TopicMirrorService
{
public:
  explicit TopicMirrorService(MirrorContext context);

  // Reads the agent's topic list and makes the local graph match it.
  // Returns false when the agent could not be asked.
  bool sync();

  // How many topics the agent advertises.
  std::size_t remote_topic_count() const;
  // The type the agent advertises for a topic, if it advertises one. The
  // uplink forwarder asks this when a local topic has no type of its own.
  bool remote_type_of(const std::string & topic, std::string & type) const;

  // The mirrors: how many, and how many of them are still streaming.
  std::size_t mirror_count() const;
  std::size_t live_mirror_count() const;

  // The counters, for the periodic report.
  void append_report(std::vector<std::string> & lines) const;

  // Drops every mirror and every graph record. Called on shutdown, and by
  // the tests that want a clean slate.
  void stop();

private:
  void startMirror(const ros2_grpc_sdk::TopicInfo & topic);
  static void onRemoteMessage(
    const std::shared_ptr<TopicMirror> & mirror, const ros2_grpc_sdk::Message & message);
  // Called with the lock held. False means the retry backoff has not elapsed
  // yet and the topic should be left alone for now.
  bool retireMirrorLocked(std::map<std::string, std::shared_ptr<TopicMirror>>::iterator it);

  MirrorContext context_;
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<TopicMirror>> mirrors_;
  std::map<std::string, MirrorRetry> retries_;
  std::map<std::string, ros2_grpc_sdk::TopicInfo> remote_topics_;
};

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_TOPIC_MIRROR_HPP
