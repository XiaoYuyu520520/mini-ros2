#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/common/status.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "ros2_grpc_sdk/export.hpp"

namespace grpc {
class Channel;
}  // namespace grpc

namespace ros2bridge {
class Ros2Bridge;
}  // namespace ros2bridge

namespace ros2_grpc_sdk {

struct DiscoveryOptions;
struct DiscoveredAgent;

struct ClientOptions {
  // LAN address of the agent, e.g. "192.168.1.20:50051".
  std::string target = "localhost:50051";
  // gRPC channel credentials are plaintext by default. Enable TLS by
  // providing a PEM root certificate and a target name override.
  bool use_tls = false;
  std::string tls_root_cert_pem;
  std::string tls_target_name_override;
  // Connection timeout used by `connect()`.
  int connect_timeout_ms = 3000;
  // Per-RPC deadline for unary calls. 0 disables the deadline.
  int rpc_timeout_ms = 5000;
  // HTTP/2 keepalive used to hold the long lived bridge connection open while
  // no RPCs are in flight.
  int keepalive_time_ms = 30000;
  int keepalive_timeout_ms = 10000;
};

// Thin, thread-safe wrapper around the generated gRPC stub.
//
// Typical use:
//
//   Ros2BridgeClient client;
//   auto st = client.connect("192.168.1.20:50051");
//   std::vector<TopicInfo> topics;
//   client.listTopics(topics);
//   client.subscribe("/chatter", "", [](const Message& m){ ... });
//   client.publish("/chatter", "std_msgs/msg/String",
//                  ros2::encodeString("hello"));
//
// A single client may be shared between threads. Each subscription owns its
// own streaming RPC and delivers callbacks on a dedicated worker thread.
class ROS2_GRPC_SDK_API Ros2BridgeClient {
 public:
  using MessageCallback = std::function<void(const Message&)>;
  using ErrorCallback = std::function<void(const Status&)>;
  // Called once the subscription stream is closed for any reason.
  using ClosedCallback = std::function<void(const Status&)>;

  Ros2BridgeClient();
  ~Ros2BridgeClient();

  Ros2BridgeClient(const Ros2BridgeClient&) = delete;
  Ros2BridgeClient& operator=(const Ros2BridgeClient&) = delete;

  // Opens a channel to the agent. Returns Unavailable when the endpoint is
  // unreachable within `options.connect_timeout_ms`.
  Status connect(const std::string& target);
  Status connect(const ClientOptions& options);
  void disconnect();
  bool connected() const;

  // Scans the LAN (all network interfaces, configurable ports) and connects to
  // the bridge agent with the lowest latency. The local machine and loopback
  // are excluded unless the options say otherwise. Requires
  // <ros2_grpc_sdk/discovery/discovery.hpp> for the option/result types.
  // When `chosen` is not null it receives the selected agent.
  Status connectToNearestAgent(const DiscoveryOptions& options,
                               DiscoveredAgent* chosen = nullptr);

  // Same as above but reads the discovery options from the config file
  // (see <ros2_grpc_sdk/common/config.hpp>). An empty path uses the standard
  // config-folder resolution.
  Status connectToNearestAgentFromConfig(const std::string& config_path = {},
                                        DiscoveredAgent* chosen = nullptr);

  // --- Unary calls ---------------------------------------------------------

  Status ping(const std::string& payload, std::string& echo_out);
  Status listTopics(std::vector<TopicInfo>& topics_out,
                    const std::string& name_filter = std::string());
  Status getTopicInfo(const std::string& topic, TopicInfo& info_out);
  Status publish(const std::string& topic, const std::string& type,
                 const Bytes& data, const std::string& encoding = "cdr");

  // --- Services ------------------------------------------------------------
  //
  // A service is one request and one response; the payloads are opaque bytes
  // (`encoding` says how to read them, "cdr" by default) because the SDK does
  // not know the caller's message types. An empty `type` asks the agent to
  // resolve it from its own graph.

  Status listServices(std::vector<ServiceInfo>& services_out,
                      const std::string& name_filter = std::string());
  Status getServiceInfo(const std::string& name, ServiceInfo& info_out);
  // Never fails because the *service* failed: a service that answered with an
  // error comes back as a successful Status with `result.success == false`, so
  // the caller can tell "the call did not happen" from "the call failed".
  Status callService(const std::string& name, const std::string& type,
                     const Bytes& request, ServiceCallResult& result_out,
                     const std::string& encoding = "cdr",
                     int timeout_ms = 0);

  // --- Actions -------------------------------------------------------------

  Status listActions(std::vector<ActionInfo>& actions_out,
                     const std::string& name_filter = std::string(),
                     bool only_active = false);
  Status getActionInfo(const std::string& name, ActionInfo& info_out);
  Status sendActionGoal(const std::string& name, const std::string& type,
                        const Bytes& goal, std::string& goal_id_out,
                        const std::string& encoding = "cdr",
                        int timeout_ms = 0);
  // Follows a goal to its end, calling `sink` for every message (feedback and
  // the terminal result). Blocks until the goal ends, `timeout_ms` expires
  // (0 = no timeout) or `sink` returns false.
  Status followActionGoal(const std::string& name, const std::string& goal_id,
                          const ActionFeedbackSink& sink, int timeout_ms = 0);
  // Polls once. `timeout_ms > 0` waits that long for the goal to end.
  Status getActionResult(const std::string& name, const std::string& goal_id,
                         ActionFeedback& result_out, int timeout_ms = 0);
  Status cancelActionGoal(const std::string& name, const std::string& goal_id,
                          int timeout_ms = 0);

  // --- Streaming subscription ---------------------------------------------

  // Starts a background streaming subscription. Returns AlreadyExists when the
  // same topic is already subscribed by this client.
  Status subscribe(const std::string& topic, const std::string& type,
                   MessageCallback on_message, ErrorCallback on_error = nullptr,
                   ClosedCallback on_closed = nullptr, int throttle_ms = 0);

  // Cancels the subscription for `topic`. The call blocks until the streaming
  // worker has stopped and the corresponding RPC has been released.
  Status unsubscribe(const std::string& topic);

  // Cancels every active subscription.
  void unsubscribeAll();

  bool isSubscribed(const std::string& topic) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ros2_grpc_sdk
