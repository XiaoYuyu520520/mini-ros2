#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

// GRPC_ARG_* constants live in grpc/impl/channel_arg_names.h from gRPC 1.55
// on; before that they were declared in grpc/impl/codegen/grpc_types.h, which
// grpcpp/grpcpp.h pulls in anyway. Asking for the header only when the
// installed gRPC actually has it is what lets this one source build against
// both: MSYS2 ships a current gRPC, while Ubuntu 24.04's libgrpc++-dev is
// still 1.51.1 and ships only grpc/impl/codegen/.
#if __has_include(<grpc/impl/channel_arg_names.h>)
#include <grpc/impl/channel_arg_names.h>
#endif
#include <grpcpp/grpcpp.h>

#include "ros2_grpc_sdk/common/config.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/discovery/discovery.hpp"
#include "ros2_grpc_sdk/version.hpp"
#include "ros2_bridge.grpc.pb.h"

namespace ros2_grpc_sdk {
namespace {

StatusCode fromGrpcCode(grpc::StatusCode code) {
  switch (code) {
    case grpc::StatusCode::OK:
      return StatusCode::Ok;
    case grpc::StatusCode::INVALID_ARGUMENT:
      return StatusCode::InvalidArgument;
    case grpc::StatusCode::NOT_FOUND:
      return StatusCode::NotFound;
    case grpc::StatusCode::ALREADY_EXISTS:
      return StatusCode::AlreadyExists;
    case grpc::StatusCode::UNAVAILABLE:
      return StatusCode::Unavailable;
    case grpc::StatusCode::DEADLINE_EXCEEDED:
      return StatusCode::Timeout;
    case grpc::StatusCode::CANCELLED:
      return StatusCode::Cancelled;
    case grpc::StatusCode::UNIMPLEMENTED:
      return StatusCode::Unimplemented;
    default:
      return StatusCode::Internal;
  }
}

Status toStatus(const grpc::Status& status) {
  if (status.ok()) {
    return Status::success();
  }
  return Status::error(fromGrpcCode(status.error_code()),
                       status.error_message());
}

Message fromProto(const ros2bridge::Message& proto) {
  Message message;
  message.topic = proto.topic();
  message.type = proto.type();
  message.encoding = proto.encoding();
  message.stamp_ns = proto.stamp_ns();
  message.sequence = proto.sequence();
  message.data.assign(proto.data().begin(), proto.data().end());
  return message;
}

GoalStatus fromProto(ros2bridge::GoalStatusCode status) {
  switch (status) {
    case ros2bridge::GOAL_STATUS_ACCEPTED:
      return GoalStatus::Accepted;
    case ros2bridge::GOAL_STATUS_EXECUTING:
      return GoalStatus::Executing;
    case ros2bridge::GOAL_STATUS_CANCELING:
      return GoalStatus::Canceling;
    case ros2bridge::GOAL_STATUS_SUCCEEDED:
      return GoalStatus::Succeeded;
    case ros2bridge::GOAL_STATUS_CANCELED:
      return GoalStatus::Canceled;
    case ros2bridge::GOAL_STATUS_ABORTED:
      return GoalStatus::Aborted;
    case ros2bridge::GOAL_STATUS_REJECTED:
      return GoalStatus::Rejected;
    default:
      return GoalStatus::Unknown;
  }
}

ActionFeedback feedbackFromProto(const ros2bridge::ActionFeedbackMessage& proto) {
  ActionFeedback out;
  out.name = proto.name();
  out.goal_id = proto.goal_id();
  out.encoding = proto.encoding();
  out.error = proto.error();
  out.status = fromProto(proto.status());
  out.feedback.assign(proto.feedback().begin(), proto.feedback().end());
  out.result.assign(proto.result().begin(), proto.result().end());
  return out;
}

void setDeadline(grpc::ClientContext& context, int timeout_ms) {
  if (timeout_ms > 0) {
    context.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::milliseconds(timeout_ms));
  }
}

}  // namespace

struct Ros2BridgeClient::Impl {
  struct Subscription {
    std::string topic;
    std::string type;
    std::shared_ptr<grpc::ClientContext> context;
    std::thread worker;
    MessageCallback on_message;
    ErrorCallback on_error;
    ClosedCallback on_closed;
    std::atomic<bool> finished{false};
  };

  ClientOptions options;
  std::shared_ptr<grpc::Channel> channel;
  std::unique_ptr<ros2bridge::Ros2Bridge::Stub> stub;
  mutable std::mutex mutex;
  std::map<std::string, std::shared_ptr<Subscription>> subscriptions;

  void runSubscription(const std::shared_ptr<Subscription>& sub,
                       int throttle_ms);
};

void Ros2BridgeClient::Impl::runSubscription(
    const std::shared_ptr<Subscription>& sub, int throttle_ms) {
  ros2bridge::SubscribeRequest request;
  request.set_topic(sub->topic);
  request.set_type(sub->type);
  request.set_throttle_ms(throttle_ms);

  Status final_status = Status::success();
  {
    std::unique_ptr<grpc::ClientReader<ros2bridge::Message>> reader(
        stub->Subscribe(sub->context.get(), request));
    ros2bridge::Message proto;
    while (reader->Read(&proto)) {
      if (sub->on_message) {
        sub->on_message(fromProto(proto));
      }
    }
    final_status = toStatus(reader->Finish());
  }

  sub->finished.store(true, std::memory_order_release);
  const bool cancelled = final_status.code() == StatusCode::Cancelled;
  if (!final_status.ok() && !cancelled) {
    if (sub->on_error) {
      sub->on_error(final_status);
    } else {
      ROS2_GRPC_SDK_WARN("subscription '", sub->topic,
                         "' ended: ", final_status.toString());
    }
  }
  if (sub->on_closed) {
    sub->on_closed(final_status);
  }
}

Ros2BridgeClient::Ros2BridgeClient() : impl_(new Impl()) {}

Ros2BridgeClient::~Ros2BridgeClient() {
  unsubscribeAll();
}

Status Ros2BridgeClient::connect(const std::string& target) {
  ClientOptions options = impl_->options;
  options.target = target;
  return connect(options);
}

Status Ros2BridgeClient::connect(const ClientOptions& options) {
  unsubscribeAll();

  std::shared_ptr<grpc::ChannelCredentials> credentials;
  if (options.use_tls) {
    grpc::SslCredentialsOptions ssl_options;
    ssl_options.pem_root_certs = options.tls_root_cert_pem;
    credentials = grpc::SslCredentials(ssl_options);
  } else {
    credentials = grpc::InsecureChannelCredentials();
  }

  grpc::ChannelArguments args;
  args.SetMaxReceiveMessageSize(64 * 1024 * 1024);
  args.SetMaxSendMessageSize(64 * 1024 * 1024);
  // Keep the connection to the agent alive across idle periods (NAT/firewall
  // timeouts, sleeping peers) so a bridge session survives without traffic.
  args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, std::max(1000, options.keepalive_time_ms));
  args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, std::max(1000, options.keepalive_timeout_ms));
  args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
  args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
  args.SetInt(GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
  args.SetInt(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
  if (!options.tls_target_name_override.empty()) {
    args.SetSslTargetNameOverride(options.tls_target_name_override);
  }

  auto channel = grpc::CreateCustomChannel(options.target, credentials, args);
  if (!channel) {
    return Status::error(StatusCode::Internal, "failed to create gRPC channel");
  }

  const auto deadline =
      std::chrono::system_clock::now() +
      std::chrono::milliseconds(std::max(1, options.connect_timeout_ms));
  if (!channel->WaitForConnected(deadline)) {
    return Status::error(StatusCode::Unavailable,
                         "could not connect to " + options.target +
                             " within the configured timeout");
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->options = options;
  impl_->channel = std::move(channel);
  impl_->stub = ros2bridge::Ros2Bridge::NewStub(impl_->channel);
  ROS2_GRPC_SDK_INFO("connected to ros2 bridge agent at ", options.target,
                     " (sdk v", version(), ")");
  return Status::success();
}

void Ros2BridgeClient::disconnect() {
  unsubscribeAll();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->stub.reset();
  impl_->channel.reset();
}

bool Ros2BridgeClient::connected() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->stub != nullptr;
}

Status Ros2BridgeClient::connectToNearestAgent(const DiscoveryOptions& options,
                                               DiscoveredAgent* chosen) {
  Ros2BridgeDiscovery discovery(options);
  DiscoveredAgent best;
  Status status = discovery.discoverBest(best);
  if (!status.ok()) {
    return status;
  }
  status = connect(best.target());
  if (!status.ok()) {
    return status;
  }
  if (chosen != nullptr) {
    *chosen = best;
  }
  ROS2_GRPC_SDK_INFO("connected to nearest agent ", best.target(), " (",
                     best.latency_ms, " ms)");
  return Status::success();
}

Status Ros2BridgeClient::connectToNearestAgentFromConfig(
    const std::string& config_path, DiscoveredAgent* chosen) {
  Config config;
  const Status status = loadConfig(config, config_path);
  if (!status.ok()) {
    return status;
  }
  return connectToNearestAgent(toDiscoveryOptions(config.discovery), chosen);
}

Status Ros2BridgeClient::ping(const std::string& payload,
                              std::string& echo_out) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::PingRequest request;
  request.set_payload(payload);
  ros2bridge::PingResponse response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->Ping(&context, request, &response);
  if (status.ok()) {
    echo_out = response.payload();
  }
  return toStatus(status);
}

Status Ros2BridgeClient::listTopics(std::vector<TopicInfo>& topics_out,
                                    const std::string& name_filter) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::ListTopicsRequest request;
  request.set_name_filter(name_filter);
  ros2bridge::ListTopicsResponse response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->ListTopics(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }

  topics_out.clear();
  topics_out.reserve(static_cast<std::size_t>(response.topics_size()));
  for (const auto& topic : response.topics()) {
    TopicInfo info;
    info.name = topic.name();
    info.type = topic.type();
    info.publisher_count = topic.publisher_count();
    info.subscriber_count = topic.subscriber_count();
    topics_out.push_back(std::move(info));
  }
  return Status::success();
}

Status Ros2BridgeClient::getTopicInfo(const std::string& topic,
                                      TopicInfo& info_out) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::GetTopicInfoRequest request;
  request.set_name(topic);
  ros2bridge::TopicInfo response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->GetTopicInfo(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  info_out.name = response.name();
  info_out.type = response.type();
  info_out.publisher_count = response.publisher_count();
  info_out.subscriber_count = response.subscriber_count();
  return Status::success();
}

Status Ros2BridgeClient::publish(const std::string& topic,
                                 const std::string& type, const Bytes& data,
                                 const std::string& encoding) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::PublishRequest request;
  request.set_topic(topic);
  request.set_type(type);
  request.set_encoding(encoding);
  request.set_data(data.data(), data.size());
  ros2bridge::PublishResponse response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->Publish(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  if (!response.accepted()) {
    return Status::error(StatusCode::Internal,
                         response.error().empty() ? "publish rejected"
                                                  : response.error());
  }
  return Status::success();
}

Status Ros2BridgeClient::subscribe(const std::string& topic,
                                   const std::string& type,
                                   MessageCallback on_message,
                                   ErrorCallback on_error,
                                   ClosedCallback on_closed, int throttle_ms) {
  if (topic.empty()) {
    return Status::error(StatusCode::InvalidArgument, "topic must not be empty");
  }

  auto subscription = std::make_shared<Impl::Subscription>();
  subscription->topic = topic;
  subscription->type = type;
  subscription->on_message = std::move(on_message);
  subscription->on_error = std::move(on_error);
  subscription->on_closed = std::move(on_closed);
  subscription->context = std::make_shared<grpc::ClientContext>();

  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    auto it = impl_->subscriptions.find(topic);
    if (it != impl_->subscriptions.end()) {
      if (!it->second->finished.load(std::memory_order_acquire)) {
        return Status::error(StatusCode::AlreadyExists,
                             "already subscribed to '" + topic + "'");
      }
      if (it->second->worker.joinable()) {
        it->second->worker.join();
      }
      impl_->subscriptions.erase(it);
    }
    impl_->subscriptions[topic] = subscription;
    subscription->worker =
        std::thread([this, subscription, throttle_ms]() {
          impl_->runSubscription(subscription, throttle_ms);
        });
  }

  ROS2_GRPC_SDK_DEBUG("subscribed to '", topic, "'");
  return Status::success();
}

Status Ros2BridgeClient::unsubscribe(const std::string& topic) {
  std::shared_ptr<Impl::Subscription> subscription;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->subscriptions.find(topic);
    if (it == impl_->subscriptions.end()) {
      return Status::error(StatusCode::NotFound,
                           "no active subscription for '" + topic + "'");
    }
    subscription = it->second;
    impl_->subscriptions.erase(it);
  }

  if (subscription->context) {
    subscription->context->TryCancel();
  }
  if (subscription->worker.joinable()) {
    subscription->worker.join();
  }
  ROS2_GRPC_SDK_DEBUG("unsubscribed from '", topic, "'");
  return Status::success();
}

void Ros2BridgeClient::unsubscribeAll() {
  std::vector<std::shared_ptr<Impl::Subscription>> subscriptions;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& entry : impl_->subscriptions) {
      subscriptions.push_back(entry.second);
    }
    impl_->subscriptions.clear();
  }
  for (auto& subscription : subscriptions) {
    if (subscription->context) {
      subscription->context->TryCancel();
    }
  }
  for (auto& subscription : subscriptions) {
    if (subscription->worker.joinable()) {
      subscription->worker.join();
    }
  }
}

bool Ros2BridgeClient::isSubscribed(const std::string& topic) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  auto it = impl_->subscriptions.find(topic);
  return it != impl_->subscriptions.end() &&
         !it->second->finished.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------- services

Status Ros2BridgeClient::listServices(std::vector<ServiceInfo>& services_out,
                                      const std::string& name_filter) {
  services_out.clear();
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::ListServicesRequest request;
  request.set_name_filter(name_filter);
  ros2bridge::ListServicesResponse response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->ListServices(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  services_out.reserve(static_cast<std::size_t>(response.services_size()));
  for (const auto& service : response.services()) {
    ServiceInfo info;
    info.name = service.name();
    info.type = service.type();
    info.source = service.source();
    services_out.push_back(std::move(info));
  }
  return Status::success();
}

Status Ros2BridgeClient::getServiceInfo(const std::string& name,
                                        ServiceInfo& info_out) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::GetServiceInfoRequest request;
  request.set_name(name);
  ros2bridge::ServiceInfo response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->GetServiceInfo(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  info_out.name = response.name();
  info_out.type = response.type();
  info_out.source = response.source();
  return Status::success();
}

Status Ros2BridgeClient::callService(const std::string& name,
                                     const std::string& type,
                                     const Bytes& request,
                                     ServiceCallResult& result_out,
                                     const std::string& encoding,
                                     int timeout_ms) {
  result_out = ServiceCallResult();
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int rpc_timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    rpc_timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::CallServiceRequest proto;
  proto.set_name(name);
  proto.set_type(type);
  proto.set_encoding(encoding);
  proto.set_request(request.data(), request.size());
  proto.set_timeout_ms(timeout_ms);
  ros2bridge::CallServiceResponse response;
  grpc::ClientContext context;
  // A service that is allowed to take a while must not be cut off by the
  // generic per-RPC deadline: the longer of the two wins.
  setDeadline(context, std::max(rpc_timeout_ms, timeout_ms));
  const grpc::Status status = stub->CallService(&context, proto, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  result_out.success = response.success();
  result_out.error = response.error();
  result_out.encoding = response.encoding();
  result_out.elapsed_ns = response.elapsed_ns();
  result_out.response.assign(response.response().begin(), response.response().end());
  return Status::success();
}

// ----------------------------------------------------------------- actions

Status Ros2BridgeClient::listActions(std::vector<ActionInfo>& actions_out,
                                     const std::string& name_filter,
                                     bool only_active) {
  actions_out.clear();
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::ListActionsRequest request;
  request.set_name_filter(name_filter);
  request.set_only_active(only_active);
  ros2bridge::ListActionsResponse response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->ListActions(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  actions_out.reserve(static_cast<std::size_t>(response.actions_size()));
  for (const auto& action : response.actions()) {
    ActionInfo info;
    info.name = action.name();
    info.type = action.type();
    info.source = action.source();
    actions_out.push_back(std::move(info));
  }
  return Status::success();
}

Status Ros2BridgeClient::getActionInfo(const std::string& name,
                                       ActionInfo& info_out) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::GetActionInfoRequest request;
  request.set_name(name);
  ros2bridge::ActionInfo response;
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);
  const grpc::Status status = stub->GetActionInfo(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  info_out.name = response.name();
  info_out.type = response.type();
  info_out.source = response.source();
  return Status::success();
}

Status Ros2BridgeClient::sendActionGoal(const std::string& name,
                                        const std::string& type,
                                        const Bytes& goal,
                                        std::string& goal_id_out,
                                        const std::string& encoding,
                                        int timeout_ms) {
  goal_id_out.clear();
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int rpc_timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    rpc_timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::SendActionGoalRequest request;
  request.set_name(name);
  request.set_type(type);
  request.set_encoding(encoding);
  request.set_goal(goal.data(), goal.size());
  request.set_timeout_ms(timeout_ms);
  ros2bridge::SendActionGoalResponse response;
  grpc::ClientContext context;
  setDeadline(context, std::max(rpc_timeout_ms, timeout_ms));
  const grpc::Status status = stub->SendActionGoal(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  if (!response.accepted()) {
    return Status::error(StatusCode::InvalidArgument,
                         response.error().empty() ? "goal was rejected"
                                                  : response.error());
  }
  goal_id_out = response.goal_id();
  return Status::success();
}

Status Ros2BridgeClient::followActionGoal(const std::string& name,
                                          const std::string& goal_id,
                                          const ActionFeedbackSink& sink,
                                          int timeout_ms) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
  }

  ros2bridge::ActionFeedbackRequest request;
  request.set_name(name);
  request.set_goal_id(goal_id);
  grpc::ClientContext context;
  setDeadline(context, timeout_ms);

  std::unique_ptr<grpc::ClientReader<ros2bridge::ActionFeedbackMessage>> reader(
      stub->ActionFeedback(&context, request));
  ros2bridge::ActionFeedbackMessage proto;
  bool stopped_by_caller = false;
  while (reader->Read(&proto)) {
    const ActionFeedback message = feedbackFromProto(proto);
    const bool terminal = message.terminal();
    if (sink && !sink(message)) {
      stopped_by_caller = true;
      break;
    }
    if (terminal) {
      break;
    }
  }
  // Leaving a stream early shows up as CANCELLED once the reader is finished;
  // that is this call's own doing, not a failure the caller asked about.
  const grpc::Status finish = reader->Finish();
  if (stopped_by_caller) {
    return Status::success();
  }
  return toStatus(finish);
}

Status Ros2BridgeClient::getActionResult(const std::string& name,
                                         const std::string& goal_id,
                                         ActionFeedback& result_out,
                                         int timeout_ms) {
  result_out = ActionFeedback();
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int rpc_timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    rpc_timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::GetActionResultRequest request;
  request.set_name(name);
  request.set_goal_id(goal_id);
  request.set_timeout_ms(timeout_ms);
  ros2bridge::GetActionResultResponse response;
  grpc::ClientContext context;
  setDeadline(context, std::max(rpc_timeout_ms, timeout_ms));
  const grpc::Status status = stub->GetActionResult(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  result_out.name = name;
  result_out.goal_id = goal_id;
  result_out.encoding = response.encoding();
  result_out.error = response.error();
  result_out.status = fromProto(response.status());
  result_out.result.assign(response.result().begin(), response.result().end());
  return Status::success();
}

Status Ros2BridgeClient::cancelActionGoal(const std::string& name,
                                          const std::string& goal_id,
                                          int timeout_ms) {
  ros2bridge::Ros2Bridge::Stub* stub = nullptr;
  int rpc_timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stub) {
      return Status::error(StatusCode::Unavailable, "client is not connected");
    }
    stub = impl_->stub.get();
    rpc_timeout_ms = impl_->options.rpc_timeout_ms;
  }

  ros2bridge::CancelActionGoalRequest request;
  request.set_name(name);
  request.set_goal_id(goal_id);
  request.set_timeout_ms(timeout_ms);
  ros2bridge::CancelActionGoalResponse response;
  grpc::ClientContext context;
  setDeadline(context, std::max(rpc_timeout_ms, timeout_ms));
  const grpc::Status status = stub->CancelActionGoal(&context, request, &response);
  if (!status.ok()) {
    return toStatus(status);
  }
  if (!response.accepted()) {
    return Status::error(StatusCode::InvalidArgument,
                         response.error().empty() ? "cancel was refused"
                                                  : response.error());
  }
  return Status::success();
}

}  // namespace ros2_grpc_sdk
