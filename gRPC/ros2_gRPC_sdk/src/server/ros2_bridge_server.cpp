#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <regex>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/version.hpp"
#include "ros2_bridge.grpc.pb.h"

namespace ros2_grpc_sdk {
namespace {

grpc::Status toGrpc(const Status& status) {
  if (status.ok()) {
    return grpc::Status::OK;
  }
  grpc::StatusCode code = grpc::StatusCode::INTERNAL;
  switch (status.code()) {
    case StatusCode::InvalidArgument:
      code = grpc::StatusCode::INVALID_ARGUMENT;
      break;
    case StatusCode::NotFound:
      code = grpc::StatusCode::NOT_FOUND;
      break;
    case StatusCode::AlreadyExists:
      code = grpc::StatusCode::ALREADY_EXISTS;
      break;
    case StatusCode::Unavailable:
      code = grpc::StatusCode::UNAVAILABLE;
      break;
    case StatusCode::Timeout:
      code = grpc::StatusCode::DEADLINE_EXCEEDED;
      break;
    case StatusCode::Cancelled:
      code = grpc::StatusCode::CANCELLED;
      break;
    case StatusCode::Unimplemented:
      code = grpc::StatusCode::UNIMPLEMENTED;
      break;
    default:
      break;
  }
  return grpc::Status(code, status.message());
}

// A backend that does not serve services/actions says so with
// `Unimplemented`; the RPC then answers NOT_FOUND the way a graph lookup that
// found nothing does, which is what a caller can act on.
grpc::Status lookupStatus(const Status& status, const char* what) {
  if (status.code() == StatusCode::Unimplemented) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
  }
  (void)what;
  return toGrpc(status);
}

ros2bridge::ServiceInfo toProto(const ServiceInfo& info) {
  ros2bridge::ServiceInfo proto;
  proto.set_name(info.name);
  proto.set_type(info.type);
  proto.set_source(info.source);
  return proto;
}

ros2bridge::ActionInfo toProto(const ActionInfo& info) {
  ros2bridge::ActionInfo proto;
  proto.set_name(info.name);
  proto.set_type(info.type);
  proto.set_source(info.source);
  return proto;
}

ros2bridge::TopicInfo toProto(const TopicInfo& info) {
  ros2bridge::TopicInfo proto;
  proto.set_name(info.name);
  proto.set_type(info.type);
  proto.set_publisher_count(info.publisher_count);
  proto.set_subscriber_count(info.subscriber_count);
  return proto;
}

}  // namespace

struct Ros2BridgeServer::Impl {
  class Service final : public ros2bridge::Ros2Bridge::Service {
   public:
    Service(IRos2Backend::Ptr backend, std::atomic<bool>* running)
        : backend_(std::move(backend)), running_(running) {}

    grpc::Status Ping(grpc::ServerContext*,
                      const ros2bridge::PingRequest* request,
                      ros2bridge::PingResponse* response) override {
      response->set_payload(request->payload());
      response->set_agent_version(version());
      response->set_ros_distro(backend_->rosDistro());
      return grpc::Status::OK;
    }

    grpc::Status ListTopics(grpc::ServerContext*,
                            const ros2bridge::ListTopicsRequest* request,
                            ros2bridge::ListTopicsResponse* response) override {
      std::vector<TopicInfo> topics;
      const Status status = backend_->listTopics(topics);
      if (!status.ok()) {
        return grpc::Status(grpc::StatusCode::INTERNAL, status.message());
      }

      std::unique_ptr<std::regex> filter;
      if (!request->name_filter().empty()) {
        try {
          filter = std::make_unique<std::regex>(request->name_filter());
        } catch (const std::regex_error& error) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              std::string("invalid name_filter: ") + error.what());
        }
      }

      for (const auto& topic : topics) {
        if (filter && !std::regex_search(topic.name, *filter)) {
          continue;
        }
        *response->add_topics() = toProto(topic);
      }
      return grpc::Status::OK;
    }

    grpc::Status GetTopicInfo(grpc::ServerContext*,
                              const ros2bridge::GetTopicInfoRequest* request,
                              ros2bridge::TopicInfo* response) override {
      std::vector<TopicInfo> topics;
      const Status status = backend_->listTopics(topics);
      if (!status.ok()) {
        return grpc::Status(grpc::StatusCode::INTERNAL, status.message());
      }
      for (const auto& topic : topics) {
        if (topic.name == request->name()) {
          *response = toProto(topic);
          return grpc::Status::OK;
        }
      }
      return grpc::Status(grpc::StatusCode::NOT_FOUND,
                          "unknown topic: " + request->name());
    }

    grpc::Status Subscribe(
        grpc::ServerContext* context,
        const ros2bridge::SubscribeRequest* request,
        grpc::ServerWriter<ros2bridge::Message>* writer) override {
      const std::string topic = request->topic();
      if (topic.empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "topic must not be empty");
      }

      struct StreamState {
        std::mutex mutex;
        bool active = true;
        std::uint64_t sequence = 0;
        grpc::ServerWriter<ros2bridge::Message>* writer = nullptr;
      };
      auto state = std::make_shared<StreamState>();
      state->writer = writer;

      IRos2Backend::SubscriptionHandle handle = 0;
      const Status status = backend_->subscribe(
          topic, request->type(),
          [state, topic](Message message) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->active || state->writer == nullptr) {
              return;
            }
            ros2bridge::Message proto;
            proto.set_topic(message.topic.empty() ? topic : message.topic);
            proto.set_type(message.type);
            proto.set_encoding(message.encoding);
            proto.set_stamp_ns(message.stamp_ns);
            proto.set_sequence(++state->sequence);
            proto.set_data(message.data.data(), message.data.size());
            if (!state->writer->Write(proto)) {
              state->active = false;
            }
          },
          handle, request->throttle_ms());

      if (!status.ok()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
      }

      ROS2_GRPC_SDK_INFO("streaming subscriber attached to '", topic, "'");

      while (!context->IsCancelled() && running_->load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->active = false;
        state->writer = nullptr;
      }
      backend_->unsubscribe(handle);
      ROS2_GRPC_SDK_INFO("streaming subscriber detached from '", topic, "'");
      return grpc::Status::OK;
    }

    grpc::Status Publish(grpc::ServerContext*,
                         const ros2bridge::PublishRequest* request,
                         ros2bridge::PublishResponse* response) override {
      Bytes data(request->data().begin(), request->data().end());
      const Status status =
          backend_->publish(request->topic(), request->type(), data,
                            request->encoding());
      response->set_accepted(status.ok());
      if (!status.ok()) {
        response->set_error(status.message());
      }
      return grpc::Status::OK;
    }

    // ---------------------------------------------------------- services

    grpc::Status ListServices(grpc::ServerContext*,
                              const ros2bridge::ListServicesRequest* request,
                              ros2bridge::ListServicesResponse* response) override {
      std::vector<ServiceInfo> services;
      const Status status = backend_->listServices(services);
      if (!status.ok()) {
        return lookupStatus(status, "services");
      }

      std::unique_ptr<std::regex> filter;
      if (!request->name_filter().empty()) {
        try {
          filter = std::make_unique<std::regex>(request->name_filter());
        } catch (const std::regex_error& error) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              std::string("invalid name_filter: ") + error.what());
        }
      }

      for (const auto& service : services) {
        if (filter && !std::regex_search(service.name, *filter)) {
          continue;
        }
        *response->add_services() = toProto(service);
      }
      return grpc::Status::OK;
    }

    grpc::Status GetServiceInfo(grpc::ServerContext*,
                                const ros2bridge::GetServiceInfoRequest* request,
                                ros2bridge::ServiceInfo* response) override {
      std::vector<ServiceInfo> services;
      const Status status = backend_->listServices(services);
      if (!status.ok()) {
        return lookupStatus(status, "services");
      }
      for (const auto& service : services) {
        if (service.name == request->name()) {
          *response = toProto(service);
          return grpc::Status::OK;
        }
      }
      return grpc::Status(grpc::StatusCode::NOT_FOUND,
                          "unknown service: " + request->name());
    }

    grpc::Status CallService(grpc::ServerContext*,
                             const ros2bridge::CallServiceRequest* request,
                             ros2bridge::CallServiceResponse* response) override {
      Bytes payload(request->request().begin(), request->request().end());
      ServiceCallResult result;
      const Status status = backend_->callService(
          request->name(), request->type(), payload, request->encoding(),
          request->timeout_ms(), result);
      if (!status.ok()) {
        return lookupStatus(status, "services");
      }
      response->set_success(result.success);
      response->set_error(result.error);
      response->set_encoding(result.encoding);
      response->set_elapsed_ns(result.elapsed_ns);
      response->set_response(result.response.data(), result.response.size());
      return grpc::Status::OK;
    }

    // ----------------------------------------------------------- actions

    grpc::Status ListActions(grpc::ServerContext*,
                             const ros2bridge::ListActionsRequest* request,
                             ros2bridge::ListActionsResponse* response) override {
      std::vector<ActionInfo> actions;
      const Status status = backend_->listActions(actions);
      if (!status.ok()) {
        return lookupStatus(status, "actions");
      }

      std::unique_ptr<std::regex> filter;
      if (!request->name_filter().empty()) {
        try {
          filter = std::make_unique<std::regex>(request->name_filter());
        } catch (const std::regex_error& error) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              std::string("invalid name_filter: ") + error.what());
        }
      }

      for (const auto& action : actions) {
        if (filter && !std::regex_search(action.name, *filter)) {
          continue;
        }
        *response->add_actions() = toProto(action);
      }
      return grpc::Status::OK;
    }

    grpc::Status GetActionInfo(grpc::ServerContext*,
                               const ros2bridge::GetActionInfoRequest* request,
                               ros2bridge::ActionInfo* response) override {
      std::vector<ActionInfo> actions;
      const Status status = backend_->listActions(actions);
      if (!status.ok()) {
        return lookupStatus(status, "actions");
      }
      for (const auto& action : actions) {
        if (action.name == request->name()) {
          *response = toProto(action);
          return grpc::Status::OK;
        }
      }
      return grpc::Status(grpc::StatusCode::NOT_FOUND,
                          "unknown action: " + request->name());
    }

    grpc::Status SendActionGoal(
        grpc::ServerContext*, const ros2bridge::SendActionGoalRequest* request,
        ros2bridge::SendActionGoalResponse* response) override {
      Bytes goal(request->goal().begin(), request->goal().end());
      std::string goal_id;
      const Status status = backend_->sendActionGoal(
          request->name(), request->type(), goal, request->encoding(),
          request->timeout_ms(), goal_id);
      // "There is no such action" is a transport level answer; a goal that
      // an existing server refuses comes back as `accepted == false`. A backend
      // reports the refusal by leaving the goal id empty - there is no goal to
      // follow up on - which is why acceptance is read off the id rather than
      // assumed.
      if (!status.ok()) {
        return toGrpc(status);
      }
      response->set_accepted(!goal_id.empty());
      response->set_goal_id(goal_id);
      return grpc::Status::OK;
    }

    grpc::Status ActionFeedback(
        grpc::ServerContext* context,
        const ros2bridge::ActionFeedbackRequest* request,
        grpc::ServerWriter<ros2bridge::ActionFeedbackMessage>* writer) override {
      if (request->goal_id().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "goal_id must not be empty");
      }
      const std::string name = request->name();
      const std::string goal_id = request->goal_id();
      const Status status = backend_->streamActionFeedback(
          name, goal_id,
          // Qualified: inside `ActionFeedback` the RPC name hides the type.
          [writer](const ::ros2_grpc_sdk::ActionFeedback& item) {
            ros2bridge::ActionFeedbackMessage proto;
            proto.set_name(item.name);
            proto.set_goal_id(item.goal_id);
            proto.set_encoding(item.encoding);
            proto.set_error(item.error);
            proto.set_status(static_cast<ros2bridge::GoalStatusCode>(
                static_cast<std::int32_t>(item.status)));
            proto.set_feedback(item.feedback.data(), item.feedback.size());
            proto.set_result(item.result.data(), item.result.size());
            return writer->Write(proto);
          },
          [context, this]() {
            return context->IsCancelled() ||
                   !running_->load(std::memory_order_acquire);
          });
      return toGrpc(status);
    }

    grpc::Status GetActionResult(
        grpc::ServerContext*, const ros2bridge::GetActionResultRequest* request,
        ros2bridge::GetActionResultResponse* response) override {
      ::ros2_grpc_sdk::ActionFeedback result;
      const Status status = backend_->getActionResult(
          request->name(), request->goal_id(), request->timeout_ms(), result);
      if (!status.ok()) {
        return lookupStatus(status, "actions");
      }
      response->set_success(result.status == GoalStatus::Succeeded);
      response->set_error(result.error);
      response->set_encoding(result.encoding);
      response->set_status(static_cast<ros2bridge::GoalStatusCode>(
          static_cast<std::int32_t>(result.status)));
      response->set_result(result.result.data(), result.result.size());
      return grpc::Status::OK;
    }

    grpc::Status CancelActionGoal(
        grpc::ServerContext*, const ros2bridge::CancelActionGoalRequest* request,
        ros2bridge::CancelActionGoalResponse* response) override {
      // Cancelling is a *request*: the backend may legitimately refuse (the
      // goal may already be finished), and that is an answer, not an error.
      const bool accepted =
          backend_->cancelActionGoal(request->name(), request->goal_id());
      response->set_accepted(accepted);
      if (!accepted) {
        response->set_error("no running goal '" + request->goal_id() + "'");
      }
      return grpc::Status::OK;
    }

   private:
    IRos2Backend::Ptr backend_;
    std::atomic<bool>* running_;
  };

  Impl(IRos2Backend::Ptr backend_in, ServerOptions options_in)
      : backend(std::move(backend_in)), options(std::move(options_in)) {}

  IRos2Backend::Ptr backend;
  ServerOptions options;
  std::unique_ptr<Service> service;
  std::unique_ptr<grpc::Server> server;
  std::atomic<bool> running{false};
};

Ros2BridgeServer::Ros2BridgeServer(IRos2Backend::Ptr backend,
                                   ServerOptions options)
    : impl_(new Impl(std::move(backend), std::move(options))) {}

Ros2BridgeServer::~Ros2BridgeServer() { stop(); }

Status Ros2BridgeServer::start() { return start(impl_->options.address); }

Status Ros2BridgeServer::start(const std::string& address) {
  if (impl_->running.load(std::memory_order_acquire)) {
    return Status::error(StatusCode::AlreadyExists, "server already running");
  }
  if (!impl_->backend) {
    return Status::error(StatusCode::InvalidArgument, "backend is null");
  }

  const Status init_status = impl_->backend->init();
  if (!init_status.ok()) {
    return init_status;
  }

  impl_->options.address = address;
  impl_->service = std::make_unique<Impl::Service>(impl_->backend, &impl_->running);

  grpc::ServerBuilder builder;
  int selected_port = 0;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials(),
                           &selected_port);
  builder.RegisterService(impl_->service.get());
  if (impl_->options.num_threads > 0) {
    builder.SetSyncServerOption(grpc::ServerBuilder::NUM_CQS,
                                impl_->options.num_threads);
    builder.SetSyncServerOption(grpc::ServerBuilder::MIN_POLLERS,
                                impl_->options.num_threads);
    builder.SetSyncServerOption(grpc::ServerBuilder::MAX_POLLERS,
                                impl_->options.num_threads);
  }

  impl_->server = builder.BuildAndStart();
  if (!impl_->server) {
    impl_->service.reset();
    return Status::error(StatusCode::Internal,
                         "failed to bind gRPC server to " + address);
  }

  impl_->running.store(true, std::memory_order_release);
  ROS2_GRPC_SDK_INFO("ros2 bridge agent listening on ", address, " backend=",
                     impl_->backend->name(), " port=", selected_port);
  return Status::success();
}

void Ros2BridgeServer::stop() {
  if (!impl_->running.exchange(false, std::memory_order_acq_rel)) {
    if (impl_->server) {
      impl_->server->Shutdown();
      impl_->server.reset();
    }
    impl_->service.reset();
    return;
  }
  if (impl_->server) {
    impl_->server->Shutdown(
        std::chrono::system_clock::now() + std::chrono::seconds(3));
    impl_->server->Wait();
    impl_->server.reset();
  }
  impl_->backend->shutdown();
  impl_->service.reset();
  ROS2_GRPC_SDK_INFO("ros2 bridge agent stopped");
}

void Ros2BridgeServer::wait() {
  if (impl_->server) {
    impl_->server->Wait();
  }
}

bool Ros2BridgeServer::running() const {
  return impl_->running.load(std::memory_order_acquire) && impl_->server != nullptr;
}

const std::string& Ros2BridgeServer::address() const {
  return impl_->options.address;
}

IRos2Backend::Ptr Ros2BridgeServer::backend() const { return impl_->backend; }

}  // namespace ros2_grpc_sdk
