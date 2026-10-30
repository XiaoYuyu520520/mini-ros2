#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/common/status.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk {

// Abstraction over "something that can see and drive a ROS2 graph".
//
// The bridge server only talks to this interface, which keeps the transport
// (gRPC) independent from the ROS2 client library. Two implementations ship
// with the SDK:
//   * MockBackend     - no ROS2 required, used for localhost development and
//                       CI on machines without a ROS2 installation.
//   * RclcppBackend   - real ROS2 backend built when ROS2 is available.
class ROS2_GRPC_SDK_API IRos2Backend {
 public:
  using Ptr = std::shared_ptr<IRos2Backend>;
  using SubscriptionHandle = std::uint64_t;
  using MessageSink = std::function<void(Message)>;

  virtual ~IRos2Backend() = default;

  // Human readable backend name, e.g. "mock" or "rclcpp".
  virtual std::string name() const = 0;

  // ROS distro string, empty when not applicable.
  virtual std::string rosDistro() const { return {}; }

  virtual Status init() = 0;
  virtual void shutdown() = 0;

  virtual Status listTopics(std::vector<TopicInfo>& topics_out) = 0;

  virtual Status publish(const std::string& topic, const std::string& type,
                         const Bytes& data, const std::string& encoding) = 0;

  // Registers `sink` to be invoked for every message received on `topic`.
  // The returned handle is used to cancel the subscription. A backend must
  // support several sinks for the same topic.
  virtual Status subscribe(const std::string& topic, const std::string& type,
                           MessageSink sink, SubscriptionHandle& handle_out,
                           int throttle_ms) = 0;

  virtual void unsubscribe(SubscriptionHandle handle) = 0;

  // --- services ----------------------------------------------------------
  //
  // A backend that cannot serve services keeps the defaults below, which
  // report `Unimplemented`; the server then answers the RPC with the same
  // code instead of pretending the graph is empty.

  virtual Status listServices(std::vector<ServiceInfo>& services_out) {
    services_out.clear();
    return unimplemented("services");
  }

  // `type` is a hint: an empty string asks the backend to resolve it from its
  // own graph. `request`/`response` are opaque bytes tagged by `encoding`.
  virtual Status callService(const std::string& name, const std::string& type,
                             const Bytes& request, const std::string& encoding,
                             int timeout_ms, ServiceCallResult& result_out) {
    (void)name;
    (void)type;
    (void)request;
    (void)encoding;
    (void)timeout_ms;
    result_out = ServiceCallResult();
    return unimplemented("services");
  }

  // --- actions -----------------------------------------------------------

  virtual Status listActions(std::vector<ActionInfo>& actions_out) {
    actions_out.clear();
    return unimplemented("actions");
  }

  virtual Status sendActionGoal(const std::string& name, const std::string& type,
                                const Bytes& goal, const std::string& encoding,
                                int timeout_ms, std::string& goal_id_out) {
    (void)name;
    (void)type;
    (void)goal;
    (void)encoding;
    (void)timeout_ms;
    goal_id_out.clear();
    return unimplemented("actions");
  }

  // `should_stop` is polled while the goal runs, so a caller that goes away
  // (a cancelled RPC) does not leave the backend streaming into nothing.
  virtual Status streamActionFeedback(
      const std::string& name, const std::string& goal_id,
      const ActionFeedbackSink& sink,
      const std::function<bool()>& should_stop) {
    (void)name;
    (void)goal_id;
    (void)sink;
    (void)should_stop;
    return unimplemented("actions");
  }

  virtual Status getActionResult(const std::string& name,
                                 const std::string& goal_id, int timeout_ms,
                                 ActionFeedback& result_out) {
    (void)name;
    (void)goal_id;
    (void)timeout_ms;
    result_out = ActionFeedback();
    return unimplemented("actions");
  }

  // Returns whether the goal was still running and has been asked to stop.
  // Cancelling an action is a request, not a command: a goal that already
  // finished is a legitimate "no".
  virtual bool cancelActionGoal(const std::string& name,
                                const std::string& goal_id) {
    (void)name;
    (void)goal_id;
    return false;
  }

 protected:
  static Status unimplemented(const char* what) {
    return Status::error(StatusCode::Unimplemented,
                         std::string("this backend does not serve ") + what);
  }
};

}  // namespace ros2_grpc_sdk
