#ifndef ROS2_BRIDGE_ACTION_PROXY_HPP
#define ROS2_BRIDGE_ACTION_PROXY_HPP

// The remote's actions, served locally.
//
// An action the robot serves has no process here to play the server role, so
// the daemon takes it: it owns the six shared-memory channels of the action
// (`_goal`, `_goal_ack`, `_cancel`, `_cancel_ack`, `_feedback`, `_result`),
// reads the goals a local caller writes, forwards them over gRPC and
// publishes the feedback and the result the robot sends back.
//
// This is the third of the daemon's four directions, and the one with the
// most state in it: a goal that is running has a follower thread of its own,
// because the streaming call that carries the feedback cannot be interleaved
// with the goal polling in the worker loop.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ros2_bridge/action_registry.hpp"
#include "ros2_bridge/daemon_options.hpp"
#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "shm_transport/shm_channel.hpp"

namespace ros2_bridge
{

// One action the robot serves, played back here by the daemon.
struct ActionProxy
{
  std::string name;
  std::string type;
  const ActionCodec * codec = nullptr;
  std::shared_ptr<shm_transport::ShmChannel> goals;
  std::shared_ptr<shm_transport::ShmChannel> goal_acks;
  std::shared_ptr<shm_transport::ShmChannel> cancels;
  std::shared_ptr<shm_transport::ShmChannel> cancel_acks;
  std::shared_ptr<shm_transport::ShmChannel> feedbacks;
  std::shared_ptr<shm_transport::ShmChannel> results;
  std::atomic<std::uint64_t> goals_sent{0};
  std::atomic<std::uint64_t> feedbacks_sent{0};
  std::atomic<std::uint64_t> results_sent{0};
  std::atomic<std::uint64_t> failures{0};
  // A local caller's goal id, mapped onto the id the robot gave the goal
  // back. Only cancel needs the pair, but a goal that nobody follows leaves
  // it behind, so it is also what says "this goal is still out there".
  std::mutex mutex;
  std::map<std::uint64_t, std::string> remote_ids;
  // The follower threads of the goals currently running: the streaming call
  // that carries feedback and the result cannot be interleaved with the goal
  // polling in the worker loop, and a goal takes as long as it takes.
  struct Follower
  {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::vector<Follower> followers;
  bool running = false;
  std::thread worker;
};

// What the action proxy needs from the rest of the daemon.
struct ActionProxyContext
{
  // The one gRPC connection. The daemon owns it; the proxy only sends on it.
  ros2_grpc_sdk::Ros2BridgeClient * client = nullptr;
  // The agent this daemon talks to, for the graph's "remote:<peer>" source.
  const std::string * peer = nullptr;
  // Whether the daemon should serve actions at all, and which ones.
  bool enabled = true;
  const std::vector<std::string> * filters = nullptr;
  // `--once`: serve the graph record but start no worker.
  bool once = false;
  // The types that were already reported as having no codec.
  std::shared_ptr<std::set<std::string>> warned_types;
  std::function<void(const char *, const std::string &)> report_scan_failure;
  std::function<void(const char *)> clear_scan_failure;
  // Whether the daemon is still running. The worker loops on it.
  std::function<bool()> running;
};

class ActionProxyService
{
public:
  explicit ActionProxyService(ActionProxyContext context);

  // Reads the agent's action list and makes the local graph match it.
  bool sync();

  std::size_t action_count() const;

  // The counters, for the periodic report.
  void append_report(std::vector<std::string> & lines) const;

  // Stops the workers and drops the graph records. The followers are joined
  // here, before the daemon tears the client down: a follower is a streaming
  // gRPC call, and it has to be out of the way before the channel goes.
  void stop();

private:
  void startAction(const ros2_grpc_sdk::ActionInfo & info, const ActionCodec * codec);
  void actionLoop(const std::shared_ptr<ActionProxy> & action);
  void handleGoal(
    const std::shared_ptr<ActionProxy> & action, const ActionCodec & codec,
    std::uint64_t id, const ros2_grpc_sdk::Bytes & goal_shim);
  void followGoal(
    const std::shared_ptr<ActionProxy> & action, std::uint64_t id,
    const std::string & remote_id);
  void handleCancel(const std::shared_ptr<ActionProxy> & action, std::uint64_t id);

  ActionProxyContext context_;
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<ActionProxy>> actions_;
};

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_ACTION_PROXY_HPP
