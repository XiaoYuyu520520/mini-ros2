#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ros2_grpc_sdk/server/i_ros2_backend.hpp"

namespace ros2_grpc_sdk {

// Configuration for the simulated ROS2 graph.
struct MockBackendOptions {
  // Topics advertised by the mock graph. When empty a sensible demo set is
  // installed (see `MockBackend::installDefaultGraph`).
  std::vector<TopicInfo> topics;
  // When true a `publish()` is looped back to every subscriber of the topic,
  // which makes round-trip testing on a single host possible.
  bool loopback = true;
  // Period of the synthetic publishers that feed the "live" demo topics.
  int autopublish_period_ms = 500;
  // When false the periodic synthetic publishers are not started; messages
  // then only arrive through `publish()` loopback.
  bool autopublish = true;
  std::string ros_distro = "mock";
  // Services advertised by the mock graph. Empty installs /demo/echo and
  // /demo/counter_reset (see `installDefaultServices`).
  std::vector<ServiceInfo> services;
  // Actions advertised by the mock graph. Empty installs /demo/count.
  std::vector<ActionInfo> actions;
};

// In-memory ROS2 stand-in. Perfectly usable on Windows/Linux developer
// machines without ROS2 installed and used by the localhost end-to-end test.
class ROS2_GRPC_SDK_API MockBackend : public IRos2Backend {
 public:
  explicit MockBackend(MockBackendOptions options = {});
  ~MockBackend() override;

  std::string name() const override { return "mock"; }
  std::string rosDistro() const override { return options_.ros_distro; }

  Status init() override;
  void shutdown() override;

  Status listTopics(std::vector<TopicInfo>& topics_out) override;

  Status publish(const std::string& topic, const std::string& type,
                 const Bytes& data, const std::string& encoding) override;

  Status subscribe(const std::string& topic, const std::string& type,
                   MessageSink sink, SubscriptionHandle& handle_out,
                   int throttle_ms) override;
  void unsubscribe(SubscriptionHandle handle) override;

  // --- services ----------------------------------------------------------
  //
  // The mock does not decode payloads, so a service call is answered by name:
  // /demo/echo hands the request bytes back, /demo/counter_reset answers with
  // a fixed payload. That is enough to exercise everything the transport does
  // with a service (list, resolve, call, error paths).

  Status listServices(std::vector<ServiceInfo>& services_out) override;
  Status callService(const std::string& name, const std::string& type,
                     const Bytes& request, const std::string& encoding,
                     int timeout_ms, ServiceCallResult& result_out) override;

  // --- actions -----------------------------------------------------------

  Status listActions(std::vector<ActionInfo>& actions_out) override;
  Status sendActionGoal(const std::string& name, const std::string& type,
                        const Bytes& goal, const std::string& encoding,
                        int timeout_ms, std::string& goal_id_out) override;
  Status streamActionFeedback(const std::string& name, const std::string& goal_id,
                              const ActionFeedbackSink& sink,
                              const std::function<bool()>& should_stop) override;
  Status getActionResult(const std::string& name, const std::string& goal_id,
                         int timeout_ms, ActionFeedback& result_out) override;
  bool cancelActionGoal(const std::string& name,
                        const std::string& goal_id) override;

  // Adds or replaces a topic in the simulated graph.
  void upsertTopic(const TopicInfo& info);

  // Installs the demo services and actions described above. The locked form
  // exists because `init()` already holds the graph mutex when it decides
  // that no services were configured.
  void installDefaultServices();
  void installDefaultServicesLocked();

  // Installs the default demo graph: /demo/chatter, /demo/counter and
  // /demo/imu.
  void installDefaultGraph();

 private:
  struct Subscription {
    SubscriptionHandle handle = 0;
    std::string topic;
    MessageSink sink;
    int throttle_ms = 0;
  };

  struct AutoSource {
    std::string topic;
    std::string type;
    std::thread worker;
    std::atomic<bool> running{false};
  };

  // One goal of a mock action: the events it has produced so far, its status
  // and its result. Feedback is replayed from the start, so a client that
  // follows late still sees the whole run.
  struct MockGoal {
    std::string name;
    std::string goal_id;
    GoalStatus status = GoalStatus::Accepted;
    bool done = false;
    bool cancel_requested = false;
    Bytes result;
    std::string error;
    std::vector<ActionFeedback> events;
    std::thread worker;
  };

  void runMockGoal(const std::shared_ptr<MockGoal>& state, int count);
  std::shared_ptr<MockGoal> findGoal(const std::string& goal_id) const;
  ActionFeedback makeEvent(const MockGoal& state, GoalStatus status,
                           const std::string& text) const;
  void autopublishLoop(const std::string& topic, const std::string& type);
  std::vector<Subscription> subscriptionsFor(const std::string& topic,
                                             std::vector<std::uint64_t>* ids = nullptr);
  void dispatch(const Message& message);

  MockBackendOptions options_;
  mutable std::mutex mutex_;
  std::map<std::string, TopicInfo> topics_;
  std::map<std::string, ServiceInfo> services_;
  std::map<std::string, ActionInfo> actions_;
  std::map<std::string, std::shared_ptr<MockGoal>> goals_;
  std::condition_variable goal_cv_;
  std::uint64_t next_goal_id_ = 1;
  std::map<SubscriptionHandle, Subscription> subscriptions_;
  std::map<SubscriptionHandle, std::int64_t> last_sent_ns_;
  std::vector<std::unique_ptr<AutoSource>> sources_;
  SubscriptionHandle next_handle_ = 1;
  std::atomic<bool> running_{false};
};

}  // namespace ros2_grpc_sdk
