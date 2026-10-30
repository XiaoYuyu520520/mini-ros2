#pragma once

#include <memory>
#include <string>

#include "ros2_grpc_sdk/server/i_ros2_backend.hpp"

// This header is only compilable when the SDK is built with ROS2 support
// (ROS2_GRPC_SDK_WITH_ROS2=ON). Its implementation lives in
// src/server/rclcpp_backend.cpp and is excluded from the build otherwise.

namespace ros2_grpc_sdk {

struct RclcppBackendOptions {
  std::string node_name = "ros2_grpc_bridge";
  std::string node_namespace = "";
  // Optional path used to initialise rclcpp when it is not already running.
  // Empty means: reuse an existing rclcpp context, otherwise initialise with
  // no arguments.
  std::string ros_args;
  // QoS depth/keep-last used for the internal generic subscriptions and
  // publishers.
  int qos_depth = 10;
  // queue size of the helper executor thread.
  std::size_t executor_threads = 1;
};

// Real ROS2 backend: lists topics from the local graph, publishes through
// generic publishers and subscribes through generic subscriptions so that no
// message type has to be known at compile time.
class ROS2_GRPC_SDK_API RclcppBackend : public IRos2Backend {
 public:
  explicit RclcppBackend(RclcppBackendOptions options = {});
  ~RclcppBackend() override;

  std::string name() const override { return "rclcpp"; }
  std::string rosDistro() const override;

  Status init() override;
  void shutdown() override;

  Status listTopics(std::vector<TopicInfo>& topics_out) override;

  Status publish(const std::string& topic, const std::string& type,
                 const Bytes& data, const std::string& encoding) override;

  Status subscribe(const std::string& topic, const std::string& type,
                   MessageSink sink, SubscriptionHandle& handle_out,
                   int throttle_ms) override;
  void unsubscribe(SubscriptionHandle handle) override;

  // Both are read straight out of the graph, so they are exact for this
  // machine. `listActions` has to be derived: ROS2 has no "actions" endpoint in
  // the graph interface, but every rclcpp_action server advertises
  // `<action>/_action/send_goal` whose type is `<pkg>/action/<Name>_SendGoal`,
  // which is enough to recover both halves of the pair.
  Status listServices(std::vector<ServiceInfo>& services_out) override;
  Status listActions(std::vector<ActionInfo>& actions_out) override;

  // One round trip against a real service. `type` may be empty, in which case
  // it is resolved from the graph. Requests and responses are CDR bytes; the
  // conversion to and from the message structs rcl actually serialises is done
  // with the service's own typesupport, loaded from the type name at run time.
  Status callService(const std::string& name, const std::string& type,
                     const Bytes& request, const std::string& encoding,
                     int timeout_ms, ServiceCallResult& result_out) override;

  // An action is addressed through three services and two topics, and the four
  // RPCs below are separate calls, so the goal id is what ties them together:
  // `sendActionGoal` records the goal it just started and the other three look
  // it up by that id. An empty `goal_id_out` means the server was reached and
  // *refused* the goal - a transport failure is reported as a failed Status
  // instead, which is the same split `ros2 action send_goal` makes.
  Status sendActionGoal(const std::string& name, const std::string& type,
                        const Bytes& goal, const std::string& encoding,
                        int timeout_ms, std::string& goal_id_out) override;

  // Streams feedback for one goal until it ends, then delivers one final
  // message whose `status` is terminal and whose `result` is the action's
  // result message. It returns early, without that message, if `should_stop`
  // starts returning true.
  Status streamActionFeedback(const std::string& name,
                              const std::string& goal_id,
                              const ActionFeedbackSink& sink,
                              const std::function<bool()>& should_stop) override;

  // A stock `rclcpp_action` server answers this only once the goal is over, so
  // a non-positive `timeout_ms` means "wait for the goal", not "return now".
  Status getActionResult(const std::string& name, const std::string& goal_id,
                         int timeout_ms, ActionFeedback& result_out) override;

  // True when a running goal was asked to stop. A goal that already finished
  // is a legitimate false.
  bool cancelActionGoal(const std::string& name,
                        const std::string& goal_id) override;

 private:
  // The action's own type (`<pkg>/action/<Name>`), from the hint the caller
  // gave or, when it gave none, from the graph: `<action>/_action/send_goal` is
  // advertised with the `<pkg>/action/<Name>_SendGoal` service type and the
  // suffix is exact.
  std::string resolveActionType(const std::string& name, const std::string& type);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ros2_grpc_sdk
