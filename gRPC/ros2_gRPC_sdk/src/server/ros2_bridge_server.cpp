#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

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

// Identity and provenance helpers. The agent is the one place that sees both
// "a message was injected through Publish" and "a subscriber is about to be
// handed that message", so it is where an echo can be tagged exactly. The
// relay then drops the tagged copy and nothing else.
std::string makeSourceId() {
  std::random_device device;
  std::ostringstream out;
  out << "agent-" << std::hex << device() << device();
  return out.str();
}

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= data[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string provenanceKey(const std::string& topic, const std::uint8_t* data,
                          std::size_t size) {
  std::ostringstream out;
  out << topic << '#' << size << '#' << fnv1a(data, size);
  return out.str();
}

}  // namespace

struct Ros2BridgeServer::Impl {
  class Service final : public ros2bridge::Ros2Bridge::Service {
   public:
    Service(IRos2Backend::Ptr backend, std::atomic<bool>* running)
        : backend_(std::move(backend)), running_(running), source_id_(makeSourceId()) {}

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

      // The backend delivers messages on its executor thread. A Write() there
      // couples that thread to the link: one slow stream (a 500 KB costmap
      // over a busy Wi-Fi link) then stalls every other topic on the same
      // agent - measured as ~0.4 Hz on /odom while a native subscriber on the
      // same host still saw 50 Hz. The callback below only appends to a
      // bounded queue; a dedicated writer thread owns the Write() call. The
      // queue keeps at most kMaxQueuedBytes per stream and drops the oldest
      // sample when a reader cannot keep up, which is the right trade for
      // live telemetry (the newest value wins) and keeps the executor
      // spinning the ROS graph no matter what the link does.
      struct StreamState {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<ros2bridge::Message> queue;
        std::size_t queued_bytes = 0;
        bool active = true;
        std::uint64_t sequence = 0;
        grpc::ServerWriter<ros2bridge::Message>* writer = nullptr;
      };
      auto state = std::make_shared<StreamState>();
      state->writer = writer;
      constexpr std::size_t kMaxQueuedBytes = 8u * 1024u * 1024u;

      auto dequeue = [state](ros2bridge::Message& out) {
        const std::size_t size = state->queue.front().data().size();
        state->queued_bytes -=
            std::min<std::size_t>(state->queued_bytes, size);
        out = std::move(state->queue.front());
        state->queue.pop_front();
      };
      auto drop_oldest = [state](std::size_t budget) {
        while (state->queued_bytes > budget && state->queue.size() > 1) {
          const std::size_t front = state->queue.front().data().size();
          state->queue.pop_front();
          state->queued_bytes -= std::min<std::size_t>(state->queued_bytes, front);
        }
      };

      std::thread pump([state, dequeue]() {
        for (;;) {
          ros2bridge::Message proto;
          {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->cv.wait(lock, [state] {
              return !state->queue.empty() || !state->active;
            });
            if (state->queue.empty()) {
              return;  // inactive and fully drained
            }
            dequeue(proto);
          }
          if (!state->writer->Write(proto)) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->active = false;
            state->queue.clear();
            state->queued_bytes = 0;
            return;
          }
        }
      });

      IRos2Backend::SubscriptionHandle handle = 0;
      const Status status = backend_->subscribe(
          topic, request->type(),
          [this, state, topic, dequeue, drop_oldest](Message message) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->active || state->writer == nullptr) {
              return;
            }
            ros2bridge::Message proto;
            const std::string message_topic =
                message.topic.empty() ? topic : message.topic;
            proto.set_topic(message_topic);
            proto.set_type(message.type);
            proto.set_encoding(message.encoding);
            proto.set_stamp_ns(message.stamp_ns);
            proto.set_sequence(++state->sequence);
            proto.set_source_id(source_id_);
            // If this is the copy of a message injected through Publish, tag
            // it with that injection's ids. This is how a relay recognises its
            // own message instead of muting a whole topic for 0.5 s and
            // dropping legitimate samples that arrive in that window.
            std::string origin_id;
            std::string trace_id;
            const std::string key =
                provenanceKey(message_topic, message.data.data(), message.data.size());
            if (takeInjection(key, origin_id, trace_id)) {
              proto.set_origin_id(origin_id);
              proto.set_trace_id(trace_id);
            }
            proto.set_data(message.data.data(), message.data.size());
            state->queued_bytes += proto.data().size();
            state->queue.push_back(std::move(proto));
            drop_oldest(kMaxQueuedBytes);
            state->cv.notify_one();
          },
          handle, request->throttle_ms());

      if (!status.ok()) {
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          state->active = false;
        }
        state->cv.notify_all();
        pump.join();
        return grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
      }

      ROS2_GRPC_SDK_INFO("streaming subscriber attached to '", topic, "'");

      while (!context->IsCancelled() && running_->load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->active = false;
      }
      state->cv.notify_all();
      pump.join();
      backend_->unsubscribe(handle);
      ROS2_GRPC_SDK_INFO("streaming subscriber detached from '", topic, "'");
      return grpc::Status::OK;
    }

    grpc::Status Publish(grpc::ServerContext*,
                         const ros2bridge::PublishRequest* request,
                         ros2bridge::PublishResponse* response) override {
      Bytes data(request->data().begin(), request->data().end());
      const auto* raw = reinterpret_cast<const std::uint8_t*>(request->data().data());
      const std::string key = provenanceKey(request->topic(), raw, request->data().size());
      const bool tracked = !request->origin_id().empty() || !request->trace_id().empty();
      if (tracked) {
        // Register *before* handing the message to the backend: the backend can
        // deliver it to the local subscriber synchronously (the mock one does),
        // and that copy must already find the record.
        rememberInjection(key, request->origin_id(), request->trace_id());
      }
      const Status status =
          backend_->publish(request->topic(), request->type(), data,
                            request->encoding());
      response->set_accepted(status.ok());
      if (!status.ok()) {
        if (tracked) {
          forgetInjection(key, request->origin_id());
        }
        response->set_error(status.message());
        switch (status.code()) {
          case StatusCode::Timeout:
            response->set_result("TIMEOUT");
            break;
          case StatusCode::Unavailable:
            response->set_result("UNAVAILABLE");
            break;
          default:
            response->set_result("REJECTED");
            break;
        }
      } else {
        response->set_result("ACCEPTED");
        if (!request->origin_id().empty()) {
          response->set_message_id(request->origin_id());
        } else {
          response->set_message_id(source_id_ + ":" + std::to_string(++published_));
        }
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
    struct Injection {
      std::string origin_id;
      std::string trace_id;
      std::chrono::steady_clock::time_point expires;
    };

    static constexpr std::size_t kMaxInjections = 4096;
    static constexpr auto kInjectionTtl = std::chrono::seconds(10);

    void pruneInjectionsLocked(std::chrono::steady_clock::time_point now) {
      for (auto it = injections_.begin(); it != injections_.end();) {
        auto& queue = it->second;
        while (!queue.empty() && queue.front().expires <= now) {
          queue.pop_front();
        }
        if (queue.empty()) {
          it = injections_.erase(it);
        } else {
          ++it;
        }
      }
    }

    void rememberInjection(const std::string& key, const std::string& origin_id,
                           const std::string& trace_id) {
      std::lock_guard<std::mutex> lock(provenance_mutex_);
      const auto now = std::chrono::steady_clock::now();
      pruneInjectionsLocked(now);
      auto& queue = injections_[key];
      queue.push_back({origin_id, trace_id, now + kInjectionTtl});
      std::size_t total = 0;
      for (const auto& entry : injections_) {
        total += entry.second.size();
      }
      while (total > kMaxInjections && !queue.empty()) {
        queue.pop_front();
        --total;
      }
      if (queue.empty()) {
        injections_.erase(key);
      }
    }

    bool takeInjection(const std::string& key, std::string& origin_id,
                       std::string& trace_id) {
      std::lock_guard<std::mutex> lock(provenance_mutex_);
      const auto now = std::chrono::steady_clock::now();
      pruneInjectionsLocked(now);
      auto found = injections_.find(key);
      if (found == injections_.end() || found->second.empty()) {
        return false;
      }
      origin_id = found->second.front().origin_id;
      trace_id = found->second.front().trace_id;
      found->second.pop_front();
      if (found->second.empty()) {
        injections_.erase(found);
      }
      return true;
    }

    void forgetInjection(const std::string& key, const std::string& origin_id) {
      std::lock_guard<std::mutex> lock(provenance_mutex_);
      auto found = injections_.find(key);
      if (found == injections_.end()) {
        return;
      }
      auto& queue = found->second;
      for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (it->origin_id == origin_id) {
          queue.erase(it);
          break;
        }
      }
      if (queue.empty()) {
        injections_.erase(found);
      }
    }

    IRos2Backend::Ptr backend_;
    std::atomic<bool>* running_;
    const std::string source_id_;
    std::atomic<std::uint64_t> published_{0};
    std::mutex provenance_mutex_;
    std::unordered_map<std::string, std::deque<Injection>> injections_;
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
  if (impl_->options.use_tls()) {
    grpc::SslServerCredentialsOptions ssl;
    ssl.pem_root_certs = impl_->options.tls_client_ca_pem;
    grpc::SslServerCredentialsOptions::PemKeyCertPair pair;
    pair.private_key = impl_->options.tls_private_key_pem;
    pair.cert_chain = impl_->options.tls_cert_chain_pem;
    ssl.pem_key_cert_pairs.push_back(std::move(pair));
    // No CA means "encrypt, do not authenticate the client". A CA means the
    // client certificate is verified against it, i.e. mutual TLS.
    ssl.client_certificate_request =
        impl_->options.tls_client_ca_pem.empty()
            ? GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE
            : GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    builder.AddListeningPort(address, grpc::SslServerCredentials(ssl),
                             &selected_port);
  } else {
    builder.AddListeningPort(address, grpc::InsecureServerCredentials(),
                             &selected_port);
  }
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
