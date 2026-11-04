#ifndef ROS2_BRIDGE_SERVICE_PROXY_HPP
#define ROS2_BRIDGE_SERVICE_PROXY_HPP

// The remote's services, served locally.
//
// A service the robot serves has no process here to answer it, so the daemon
// takes that role: it owns the two shared-memory channels of the service
// (`<name>/_request` and `<name>/_response`), reads the requests a local
// caller writes, forwards them over gRPC and publishes the answer back. From
// the Windows side the pair is indistinguishable from a service served in
// this machine's own process.
//
// This is the second of the daemon's four directions, and the first one that
// needed a thread of its own.

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

#include "ros2_bridge/daemon_options.hpp"
#include "ros2_bridge/service_registry.hpp"
#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "shm_transport/shm_channel.hpp"

namespace ros2_bridge
{

// One service the robot serves, answered here by the daemon.
struct ServiceProxy
{
  std::string name;
  std::string type;
  const ServiceCodec * codec = nullptr;
  std::shared_ptr<shm_transport::ShmChannel> requests;
  std::shared_ptr<shm_transport::ShmChannel> responses;
  std::atomic<std::uint64_t> calls{0};
  std::atomic<std::uint64_t> failures{0};
  std::atomic<std::int64_t> last_elapsed_ns{0};
  bool running = false;
  std::thread worker;
};

// What the service proxy needs from the rest of the daemon.
struct ServiceProxyContext
{
  // The one gRPC connection. The daemon owns it; the proxy only sends on it.
  ros2_grpc_sdk::Ros2BridgeClient * client = nullptr;
  // The agent this daemon talks to, for the graph's "remote:<peer>" source.
  const std::string * peer = nullptr;
  // Whether the daemon should serve services at all, and which ones.
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

class ServiceProxyService
{
public:
  explicit ServiceProxyService(ServiceProxyContext context);

  // Reads the agent's service list and makes the local graph match it.
  bool sync();

  std::size_t service_count() const;

  // The counters, for the periodic report.
  void append_report(std::vector<std::string> & lines) const;

  // Stops the workers and drops the graph records. The workers are joined
  // here, before the daemon tears the client down: a worker blocked in a
  // service call has to be out of the way before the channel goes.
  void stop();

private:
  void startService(const ros2_grpc_sdk::ServiceInfo & info, const ServiceCodec * codec);
  void serviceLoop(const std::shared_ptr<ServiceProxy> & service);

  ServiceProxyContext context_;
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<ServiceProxy>> services_;
};

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_SERVICE_PROXY_HPP
