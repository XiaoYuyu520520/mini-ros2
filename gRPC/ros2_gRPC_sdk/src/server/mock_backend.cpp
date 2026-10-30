#include "ros2_grpc_sdk/server/mock_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/ros2_types.hpp"

namespace ros2_grpc_sdk {
namespace {

std::int64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool supportsAutopublish(const TopicInfo& info) {
  return info.type == "std_msgs/msg/String" ||
         info.type == "example_interfaces/msg/Int64" ||
         info.type == "std_msgs/msg/Int64";
}

}  // namespace

MockBackend::MockBackend(MockBackendOptions options)
    : options_(std::move(options)) {}

void MockBackend::installDefaultServices() {
  std::lock_guard<std::mutex> lock(mutex_);
  installDefaultServicesLocked();
}

void MockBackend::installDefaultServicesLocked() {
  const auto addService = [this](const std::string& name, const std::string& type) {
    ServiceInfo info;
    info.name = name;
    info.type = type;
    info.source = "local";
    services_[name] = info;
  };
  addService("/demo/echo", "std_msgs/msg/String");
  addService("/demo/counter_reset", "std_srvs/srv/Trigger");

  const auto addAction = [this](const std::string& name, const std::string& type) {
    ActionInfo info;
    info.name = name;
    info.type = type;
    info.source = "local";
    actions_[name] = info;
  };
  addAction("/demo/count", "example_interfaces/action/Fibonacci");
}

MockBackend::~MockBackend() { shutdown(); }

void MockBackend::installDefaultGraph() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto add = [this](const std::string& name, const std::string& type) {
    TopicInfo info;
    info.name = name;
    info.type = type;
    info.publisher_count = 1;
    topics_[name] = info;
  };
  add("/demo/chatter", "std_msgs/msg/String");
  add("/demo/counter", "example_interfaces/msg/Int64");
  add("/demo/imu", "sensor_msgs/msg/Imu");
  add("/rosout", "rcl_interfaces/msg/Log");
}

Status MockBackend::init() {
  if (running_.load(std::memory_order_acquire)) {
    return Status::success();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& topic : options_.topics) {
    topics_[topic.name] = topic;
  }
  if (topics_.empty()) {
    const auto add = [this](const std::string& name, const std::string& type) {
      TopicInfo info;
      info.name = name;
      info.type = type;
      info.publisher_count = 1;
      topics_[name] = info;
    };
    add("/demo/chatter", "std_msgs/msg/String");
    add("/demo/counter", "example_interfaces/msg/Int64");
    add("/demo/imu", "sensor_msgs/msg/Imu");
    add("/rosout", "rcl_interfaces/msg/Log");
  }

  for (const auto& service : options_.services) {
    services_[service.name] = service;
  }
  for (const auto& action : options_.actions) {
    actions_[action.name] = action;
  }
  if (services_.empty() && actions_.empty()) {
    installDefaultServicesLocked();
  }
  running_.store(true, std::memory_order_release);

  if (options_.autopublish) {
    for (const auto& entry : topics_) {
      if (!supportsAutopublish(entry.second)) {
        continue;
      }
      auto source = std::make_unique<AutoSource>();
      source->topic = entry.first;
      source->type = entry.second.type;
      source->running.store(true, std::memory_order_release);
      AutoSource* raw = source.get();
      source->worker = std::thread([this, raw]() {
        autopublishLoop(raw->topic, raw->type);
      });
      sources_.push_back(std::move(source));
    }
  }

  ROS2_GRPC_SDK_INFO("mock backend initialised with ", topics_.size(),
                     " topics, ", services_.size(), " services and ",
                     actions_.size(), " action(s) (autopublish=",
                     options_.autopublish ? "on" : "off", ")");
  return Status::success();
}

void MockBackend::shutdown() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  for (auto& source : sources_) {
    source->running.store(false, std::memory_order_release);
  }
  for (auto& source : sources_) {
    if (source->worker.joinable()) {
      source->worker.join();
    }
  }
  sources_.clear();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : goals_) {
      if (entry.second) {
        entry.second->cancel_requested = true;
      }
    }
    goal_cv_.notify_all();
  }
  for (auto& entry : goals_) {
    if (entry.second && entry.second->worker.joinable()) {
      entry.second->worker.join();
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    goals_.clear();
    subscriptions_.clear();
  }
  ROS2_GRPC_SDK_INFO("mock backend shut down");
}

Status MockBackend::listTopics(std::vector<TopicInfo>& topics_out) {
  std::lock_guard<std::mutex> lock(mutex_);
  topics_out.clear();
  topics_out.reserve(topics_.size());
  for (auto& entry : topics_) {
    TopicInfo info = entry.second;
    int subscribers = 0;
    int publishers = info.publisher_count;
    for (const auto& subscription : subscriptions_) {
      if (subscription.second.topic == info.name) {
        ++subscribers;
      }
    }
    info.subscriber_count = subscribers;
    info.publisher_count = publishers;
    topics_out.push_back(std::move(info));
  }
  return Status::success();
}

Status MockBackend::publish(const std::string& topic, const std::string& type,
                            const Bytes& data, const std::string& encoding) {
  if (topic.empty()) {
    return Status::error(StatusCode::InvalidArgument, "topic must not be empty");
  }
  if (!running_.load(std::memory_order_acquire)) {
    return Status::error(StatusCode::Unavailable, "mock backend is not running");
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = topics_.find(topic);
    if (it == topics_.end()) {
      TopicInfo info;
      info.name = topic;
      info.type = type;
      info.publisher_count = 0;
      topics_[topic] = info;
    } else if (it->second.type.empty() && !type.empty()) {
      it->second.type = type;
    }
  }

  Message message;
  message.topic = topic;
  message.type = type;
  message.data = data;
  message.encoding = encoding.empty() ? "cdr" : encoding;
  message.stamp_ns = nowNs();

  ROS2_GRPC_SDK_DEBUG("mock publish ", topic, " type=", type, " bytes=",
                      data.size());

  if (options_.loopback) {
    dispatch(message);
  }
  return Status::success();
}

Status MockBackend::subscribe(const std::string& topic, const std::string& type,
                              MessageSink sink, SubscriptionHandle& handle_out,
                              int throttle_ms) {
  if (topic.empty()) {
    return Status::error(StatusCode::InvalidArgument, "topic must not be empty");
  }
  if (!sink) {
    return Status::error(StatusCode::InvalidArgument, "sink must be set");
  }
  if (!running_.load(std::memory_order_acquire)) {
    return Status::error(StatusCode::Unavailable, "mock backend is not running");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  Subscription subscription;
  subscription.handle = next_handle_++;
  subscription.topic = topic;
  subscription.sink = std::move(sink);
  subscription.throttle_ms = throttle_ms;
  handle_out = subscription.handle;
  subscriptions_[subscription.handle] = std::move(subscription);

  if (!type.empty() && topics_.find(topic) == topics_.end()) {
    TopicInfo info;
    info.name = topic;
    info.type = type;
    topics_[topic] = info;
  }
  return Status::success();
}

void MockBackend::unsubscribe(SubscriptionHandle handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  subscriptions_.erase(handle);
}

void MockBackend::upsertTopic(const TopicInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  topics_[info.name] = info;
}

std::vector<MockBackend::Subscription> MockBackend::subscriptionsFor(
    const std::string& topic, std::vector<std::uint64_t>* ids) {
  std::vector<Subscription> out;
  for (const auto& entry : subscriptions_) {
    if (entry.second.topic == topic) {
      out.push_back(entry.second);
      if (ids) {
        ids->push_back(entry.first);
      }
    }
  }
  return out;
}

void MockBackend::dispatch(const Message& message) {
  std::vector<Subscription> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::int64_t now = nowNs();
    for (const auto& entry : subscriptions_) {
      const Subscription& subscription = entry.second;
      if (subscription.topic != message.topic) {
        continue;
      }
      if (subscription.throttle_ms > 0) {
        const auto last = last_sent_ns_.find(entry.first);
        const std::int64_t throttle_ns =
            static_cast<std::int64_t>(subscription.throttle_ms) * 1000000;
        if (last != last_sent_ns_.end() &&
            now - last->second < throttle_ns) {
          continue;
        }
        last_sent_ns_[entry.first] = now;
      }
      targets.push_back(subscription);
    }
  }

  for (auto& subscription : targets) {
    if (subscription.sink) {
      subscription.sink(message);
    }
  }
}

void MockBackend::autopublishLoop(const std::string& topic,
                                  const std::string& type) {
  std::uint64_t counter = 0;
  const int period_ms = options_.autopublish_period_ms > 0
                            ? options_.autopublish_period_ms
                            : 500;
  while (running_.load(std::memory_order_acquire)) {
    Message message;
    message.topic = topic;
    message.type = type;
    message.encoding = "cdr";
    message.stamp_ns = nowNs();
    message.sequence = counter;

    if (type == "std_msgs/msg/String") {
      message.data = ros2::encodeString("mock " + topic + " #" +
                                        std::to_string(counter));
    } else if (type == "example_interfaces/msg/Int64" ||
               type == "std_msgs/msg/Int64") {
      message.data = ros2::encodeInt64(static_cast<std::int64_t>(counter));
    } else {
      message.data = ros2::encodeString("mock " + topic);
    }
    ++counter;

    dispatch(message);

    for (int waited = 0; waited < period_ms; waited += 25) {
      if (!running_.load(std::memory_order_acquire)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
  }
}

// ---------------------------------------------------------------- services

Status MockBackend::listServices(std::vector<ServiceInfo>& services_out) {
  std::lock_guard<std::mutex> lock(mutex_);
  services_out.clear();
  services_out.reserve(services_.size());
  for (const auto& entry : services_) {
    services_out.push_back(entry.second);
  }
  return Status::success();
}

Status MockBackend::callService(const std::string& name, const std::string& type,
                                const Bytes& request,
                                const std::string& encoding, int timeout_ms,
                                ServiceCallResult& result_out) {
  (void)type;
  (void)timeout_ms;
  result_out = ServiceCallResult();
  result_out.encoding = encoding.empty() ? "cdr" : encoding;

  std::lock_guard<std::mutex> lock(mutex_);
  if (services_.find(name) == services_.end()) {
    // A service that is not there is an *answer* ("there is no such
    // service"), not a transport failure: the RPC succeeded, the call did
    // not. Callers on the other side of the bridge see the same distinction.
    result_out.success = false;
    result_out.error = "no service named '" + name + "'";
    return Status::success();
  }

  if (name == "/demo/echo") {
    result_out.response = request;
  } else {
    const std::string payload = "reset ok";
    result_out.response.assign(payload.begin(), payload.end());
  }
  result_out.success = true;
  return Status::success();
}

// ----------------------------------------------------------------- actions

Status MockBackend::listActions(std::vector<ActionInfo>& actions_out) {
  std::lock_guard<std::mutex> lock(mutex_);
  actions_out.clear();
  actions_out.reserve(actions_.size());
  for (const auto& entry : actions_) {
    actions_out.push_back(entry.second);
  }
  return Status::success();
}

std::shared_ptr<MockBackend::MockGoal> MockBackend::findGoal(
    const std::string& goal_id) const {
  const auto it = goals_.find(goal_id);
  return it == goals_.end() ? nullptr : it->second;
}

ActionFeedback MockBackend::makeEvent(const MockGoal& state, GoalStatus status,
                                      const std::string& text) const {
  ActionFeedback event;
  event.name = state.name;
  event.goal_id = state.goal_id;
  event.status = status;
  event.encoding = "mock";
  if (!text.empty()) {
    event.feedback.assign(text.begin(), text.end());
  }
  if (isTerminal(status)) {
    event.result = state.result;
    event.error = state.error;
  }
  return event;
}

Status MockBackend::sendActionGoal(const std::string& name,
                                   const std::string& type, const Bytes& goal,
                                   const std::string& encoding, int timeout_ms,
                                   std::string& goal_id_out) {
  (void)type;
  (void)encoding;
  (void)timeout_ms;
  goal_id_out.clear();

  std::shared_ptr<MockGoal> state;
  int count = 3;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (actions_.find(name) == actions_.end()) {
      return Status::error(StatusCode::NotFound,
                           "no action named '" + name + "'");
    }
    // The mock has no schema, so the goal is read as a plain decimal number:
    // "how many feedback messages to produce before succeeding".
    const std::string text(goal.begin(), goal.end());
    if (!text.empty()) {
      const int parsed = std::atoi(text.c_str());
      if (parsed > 0 && parsed <= 1000) {
        count = parsed;
      }
    }
    state = std::make_shared<MockGoal>();
    state->name = name;
    state->goal_id = "mock-goal-" + std::to_string(next_goal_id_++);
    state->status = GoalStatus::Accepted;
    goals_[state->goal_id] = state;
    goal_id_out = state->goal_id;
  }

  state->worker = std::thread([this, state, count]() {
    runMockGoal(state, count);
  });
  return Status::success();
}

void MockBackend::runMockGoal(const std::shared_ptr<MockGoal>& state,
                              int count) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state->status = GoalStatus::Executing;
    state->events.push_back(makeEvent(*state, GoalStatus::Executing, {}));
  }
  goal_cv_.notify_all();

  for (int i = 1; i <= count; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::lock_guard<std::mutex> lock(mutex_);
    if (state->cancel_requested) {
      state->status = GoalStatus::Canceled;
      state->error = "canceled after " + std::to_string(i - 1) + " step(s)";
      state->result.assign(state->error.begin(), state->error.end());
      state->events.push_back(makeEvent(*state, GoalStatus::Canceled, {}));
      state->done = true;
      goal_cv_.notify_all();
      return;
    }
    state->events.push_back(
        makeEvent(*state, GoalStatus::Executing, std::to_string(i)));
    goal_cv_.notify_all();
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state->status = GoalStatus::Succeeded;
    const std::string summary = "counted to " + std::to_string(count);
    state->result.assign(summary.begin(), summary.end());
    state->events.push_back(makeEvent(*state, GoalStatus::Succeeded, {}));
    state->done = true;
  }
  goal_cv_.notify_all();
}

Status MockBackend::streamActionFeedback(const std::string& name,
                                         const std::string& goal_id,
                                         const ActionFeedbackSink& sink,
                                         const std::function<bool()>& should_stop) {
  (void)name;
  std::unique_lock<std::mutex> lock(mutex_);
  const std::shared_ptr<MockGoal> state = findGoal(goal_id);
  if (!state) {
    return Status::error(StatusCode::NotFound, "no goal '" + goal_id + "'");
  }

  std::size_t index = 0;
  while (true) {
    while (index >= state->events.size() && !state->done) {
      goal_cv_.wait_for(lock, std::chrono::milliseconds(50));
      if (should_stop && should_stop()) {
        return Status::success();
      }
    }
    if (index >= state->events.size() && state->done) {
      return Status::success();
    }
    const ActionFeedback event = state->events[index++];
    const bool last = state->done && index >= state->events.size();
    lock.unlock();
    const bool keep_going = !sink || sink(event);
    lock.lock();
    if (!keep_going || last) {
      return Status::success();
    }
  }
}

Status MockBackend::getActionResult(const std::string& name,
                                    const std::string& goal_id, int timeout_ms,
                                    ActionFeedback& result_out) {
  (void)name;
  result_out = ActionFeedback();
  std::unique_lock<std::mutex> lock(mutex_);
  const std::shared_ptr<MockGoal> state = findGoal(goal_id);
  if (!state) {
    return Status::error(StatusCode::NotFound, "no goal '" + goal_id + "'");
  }
  if (timeout_ms > 0) {
    goal_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                      [&state]() { return state->done; });
  }
  result_out.name = state->name;
  result_out.goal_id = state->goal_id;
  result_out.status = state->status;
  result_out.encoding = "mock";
  result_out.result = state->result;
  result_out.error = state->done ? state->error : std::string("goal is still running");
  return Status::success();
}

bool MockBackend::cancelActionGoal(const std::string& name,
                                   const std::string& goal_id) {
  (void)name;
  std::lock_guard<std::mutex> lock(mutex_);
  const std::shared_ptr<MockGoal> state = findGoal(goal_id);
  if (!state || state->done) {
    return false;
  }
  state->cancel_requested = true;
  return true;
}

}  // namespace ros2_grpc_sdk
