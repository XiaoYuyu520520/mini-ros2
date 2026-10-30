// Real ROS2 backend. This translation unit is only compiled when the SDK is
// configured with -DROS2_GRPC_SDK_WITH_ROS2=ON and a ROS2 installation is
// available. It is intentionally kept free of any gRPC dependency.

#include "ros2_grpc_sdk/server/rclcpp_backend.hpp"

#if defined(ROS2_GRPC_SDK_WITH_ROS2)

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/generic_client.hpp>
#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rclcpp/typesupport_helpers.hpp>

#include <rcutils/allocator.h>
#include <rcutils/error_handling.h>
#include <rmw/rmw.h>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <rosidl_runtime_c/message_type_support_struct.h>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

#include "ros2_grpc_sdk/common/log.hpp"

namespace ros2_grpc_sdk {
namespace {

std::string currentRosDistro() {
  if (const char* distro = std::getenv("ROS_DISTRO")) {
    return distro;
  }
  return {};
}

Bytes toBytes(const rclcpp::SerializedMessage& message) {
  const rcl_serialized_message_t& raw = message.get_rcl_serialized_message();
  return Bytes(raw.buffer, raw.buffer + raw.buffer_length);
}

rclcpp::SerializedMessage toSerialized(const Bytes& data) {
  rclcpp::SerializedMessage serialized(data.size());
  rcl_serialized_message_t& raw = serialized.get_rcl_serialized_message();
  if (!data.empty()) {
    std::memcpy(raw.buffer, data.data(), data.size());
  }
  raw.buffer_length = data.size();
  return serialized;
}

// ROS2's graph interface has topics and services but no "actions": an action is
// a naming convention plus three services and two topics. `rclcpp_action`
// advertises `<action>/_action/send_goal` with the type
// `<pkg>/action/<Name>_SendGoal`, and those two strings are enough to recover
// the action's name and type - the same pair `ros2 action list -t` prints.
bool actionNameFromSendGoal(const std::string& service_name,
                            std::string& action_out) {
  static const std::string kSuffix = "/_action/send_goal";
  if (service_name.size() <= kSuffix.size() ||
      service_name.compare(service_name.size() - kSuffix.size(),
                           kSuffix.size(), kSuffix) != 0) {
    return false;
  }
  action_out = service_name.substr(0, service_name.size() - kSuffix.size());
  return true;
}

bool actionTypeFromSendGoalType(const std::string& service_type,
                                std::string& type_out) {
  static const std::string kSuffix = "_SendGoal";
  if (service_type.size() <= kSuffix.size() ||
      service_type.compare(service_type.size() - kSuffix.size(),
                           kSuffix.size(), kSuffix) != 0) {
    return false;
  }
  type_out = service_type.substr(0, service_type.size() - kSuffix.size());
  return true;
}

const rosidl_typesupport_introspection_cpp::MessageMembers * introspectionMembers(
    const rosidl_message_type_support_t * type_support) {
  // The 2-argument form lives in rosidl_runtime_c (the `_cpp` one is the
  // dispatch header and wants a library as well).
  const auto * handle = get_message_typesupport_handle(
      type_support, rosidl_typesupport_introspection_cpp::typesupport_identifier);
  return static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
      handle->data);
}

// rcl does not exchange bytes with a service, it exchanges *message structs*:
// `rcl_send_request` serialises the request itself, and a response arrives as a
// zero-initialised instance of the response message (that is the `void *` in
// `GenericClient::Response`). The bridge speaks CDR, so `callService` has to
// cross that gap on both sides - which it does with the service's own
// typesupport, loaded from the type name at run time, exactly like
// `GenericClient` loads it internally. This is the pair of RAII helpers for
// that crossing.
struct ServiceTypeSupport {
  std::shared_ptr<rcpputils::SharedLibrary> library;
  const rosidl_message_type_support_t * request = nullptr;
  const rosidl_message_type_support_t * response = nullptr;

  explicit ServiceTypeSupport(const std::string & service_type) {
    library = rclcpp::get_typesupport_library(service_type, "rosidl_typesupport_cpp");
    const auto * service = rclcpp::get_service_typesupport_handle(
        service_type, "rosidl_typesupport_cpp", *library);
    request = service->request_typesupport;
    response = service->response_typesupport;
  }
};

// One request/response message struct, allocated and initialised the way the
// introspection typesupport says it must be, and torn down again on the way
// out - including the early returns, which is why it is not a bare `new`.
struct MessageInstance {
  const rosidl_typesupport_introspection_cpp::MessageMembers * members;
  void * data;

  explicit MessageInstance(
      const rosidl_typesupport_introspection_cpp::MessageMembers * members_in)
      : members(members_in), data(new std::uint8_t[members_in->size_of_]) {
    members->init_function(data, rosidl_runtime_cpp::MessageInitialization::ZERO);
  }

  ~MessageInstance() {
    members->fini_function(data);
    delete[] reinterpret_cast<std::uint8_t *>(data);
  }

  MessageInstance(const MessageInstance &) = delete;
  MessageInstance & operator=(const MessageInstance &) = delete;
};

// --- actions ---------------------------------------------------------------
//
// An action is not one entity in the ROS2 graph: it is a naming convention
// plus three services and two topics, and the four action RPCs are separate
// calls, so a goal has to be remembered between them (see Impl::goals). Every
// shape below is derived from the action's type name at run time, so any
// action the graph advertises works - nothing here is specific to Fibonacci or
// to nav2.
//
// Two Jazzy facts the implementation depends on, both measured against a stock
// `rclcpp_action` server rather than read off a tutorial:
//   * `MessageMember::members_` advertises the *introspection* typesupport, and
//     `rmw_serialize` / `rmw_deserialize` reject it outright with "type support
//     not from this implementation". Payloads therefore go through a handle
//     loaded from the type name (MessageTypeSupport); the two agree on layout,
//     which is why offsets may still be read off whichever table is at hand.
//   * `_SendGoal_Response` is `{bool accepted; builtin_interfaces/Time stamp;}`,
//     not the two-field struct older tutorials describe.

const rosidl_typesupport_introspection_cpp::MessageMember * findMember(
    const rosidl_typesupport_introspection_cpp::MessageMembers * members,
    const char * name) {
  for (std::uint32_t i = 0; i < members->member_count_; ++i) {
    if (std::strcmp(members->members_[i].name_, name) == 0) {
      return &members->members_[i];
    }
  }
  return nullptr;
}

// The byte offset of a member named by a path of nested message members, e.g.
// {"goal_info", "goal_id", "uuid"} for a cancel request. Offsets are relative
// to the start of their own struct, so they add up.
std::uint32_t memberOffset(
    const rosidl_typesupport_introspection_cpp::MessageMembers * members,
    const std::vector<const char *> & path) {
  std::uint32_t offset = 0;
  const rosidl_typesupport_introspection_cpp::MessageMembers * current = members;
  for (const char * step : path) {
    const rosidl_typesupport_introspection_cpp::MessageMember * member =
        findMember(current, step);
    if (member == nullptr) {
      throw std::runtime_error(std::string("no member '") + step + "'");
    }
    offset += member->offset_;
    if (member->members_ != nullptr) {
      current = introspectionMembers(member->members_);
    }
  }
  return offset;
}

// The introspection table of a nested member, e.g. GoalStatusArray's
// `status_list` -> GoalStatus.
const rosidl_typesupport_introspection_cpp::MessageMembers * memberMembers(
    const rosidl_typesupport_introspection_cpp::MessageMembers * members,
    const char * name) {
  const rosidl_typesupport_introspection_cpp::MessageMember * member =
      findMember(members, name);
  if (member == nullptr || member->members_ == nullptr) {
    throw std::runtime_error(std::string("no message member '") + name + "'");
  }
  return introspectionMembers(member->members_);
}

// A message type loaded from its name at run time - the only kind of handle
// rmw will serialise through, see the note above.
struct MessageTypeSupport {
  std::shared_ptr<rcpputils::SharedLibrary> library;
  const rosidl_message_type_support_t * handle = nullptr;

  explicit MessageTypeSupport(const std::string & type) {
    library = rclcpp::get_typesupport_library(type, "rosidl_typesupport_cpp");
    handle = rclcpp::get_message_typesupport_handle(
        type, "rosidl_typesupport_cpp", *library);
  }
};

const std::uint8_t * at(const void * base, std::uint32_t offset) {
  return static_cast<const std::uint8_t *>(base) + offset;
}

std::uint8_t * at(void * base, std::uint32_t offset) {
  return static_cast<std::uint8_t *>(base) + offset;
}

// A goal id has to survive between four RPCs, so it travels as the 32 hex
// digits of the 16 byte UUID that rclcpp_action already uses for the goal.
std::string bytesToHex(const std::uint8_t * bytes, std::size_t size) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(kDigits[bytes[i] >> 4]);
    out.push_back(kDigits[bytes[i] & 0x0f]);
  }
  return out;
}

bool hexToBytes(const std::string & text, std::uint8_t * out, std::size_t size) {
  if (text.size() != size * 2) {
    return false;
  }
  for (std::size_t i = 0; i < size; ++i) {
    int nibbles[2] = {0, 0};
    for (int half = 0; half < 2; ++half) {
      const char c = text[i * 2 + half];
      if (c >= '0' && c <= '9') {
        nibbles[half] = c - '0';
      } else if (c >= 'a' && c <= 'f') {
        nibbles[half] = c - 'a' + 10;
      } else if (c >= 'A' && c <= 'F') {
        nibbles[half] = c - 'A' + 10;
      } else {
        return false;
      }
    }
    out[i] = static_cast<std::uint8_t>((nibbles[0] << 4) | nibbles[1]);
  }
  return true;
}

std::array<std::uint8_t, 16> randomUuid() {
  // A shared generator behind a lock: `std::random_device` is slow enough to
  // matter when a planner sends goals in a loop, and the engine is not
  // thread safe.
  static std::mutex mutex;
  static std::mt19937_64 generator(std::random_device{}());
  std::lock_guard<std::mutex> lock(mutex);
  std::array<std::uint8_t, 16> uuid{};
  for (std::size_t i = 0; i < uuid.size(); i += sizeof(std::uint64_t)) {
    const std::uint64_t word = generator();
    std::memcpy(uuid.data() + i, &word, sizeof(word));
  }
  return uuid;
}

// The answer of an action RPC is a *sub-message* of the service response
// (`result`, `feedback`), so it is serialised out of the response struct the
// same way `callService` serialises a whole response.
bool serializeAt(const void * base, std::uint32_t offset,
                 const rosidl_message_type_support_t * type, Bytes & out,
                 std::string & error_out) {
  out.clear();
  rmw_serialized_message_t serialized = rmw_get_zero_initialized_serialized_message();
  const rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (rmw_serialized_message_init(&serialized, 0u, &allocator) != RMW_RET_OK) {
    error_out = "could not allocate a serialized message";
    return false;
  }
  if (rmw_serialize(at(base, offset), type, &serialized) != RMW_RET_OK) {
    error_out = rcutils_get_error_string().str;
    rcutils_reset_error();
    rmw_serialized_message_fini(&serialized);
    return false;
  }
  out.assign(serialized.buffer, serialized.buffer + serialized.buffer_length);
  rmw_serialized_message_fini(&serialized);
  return true;
}

// Loads a request/response payload (CDR) into a struct field of a message.
bool deserializeInto(const Bytes & data,
                     const rosidl_message_type_support_t * type, void * target,
                     std::string & error_out) {
  // A non-owning view over the caller's bytes: rmw_deserialize only reads.
  rmw_serialized_message_t serialized = rmw_get_zero_initialized_serialized_message();
  serialized.buffer = const_cast<std::uint8_t *>(data.data());
  serialized.buffer_length = data.size();
  serialized.buffer_capacity = data.size();
  serialized.allocator = rcutils_get_default_allocator();
  if (rmw_deserialize(&serialized, type, target) != RMW_RET_OK) {
    error_out = rcutils_get_error_string().str;
    rcutils_reset_error();
    return false;
  }
  return true;
}

}  // namespace

struct RclcppBackend::Impl {
  struct Subscription {
    rclcpp::GenericSubscription::SharedPtr subscription;
    std::string type;
  };

  // An action goal in flight. `sendActionGoal` is the only call that creates
  // one; the other three RPCs are addressed by goal id alone, which is why
  // this exists at all.
  struct Goal {
    std::string name;
    std::string type;
    std::array<std::uint8_t, 16> uuid{};
  };

  // Long enough that a caller can still ask for a result well after the goal
  // ended, small enough that an agent running for weeks does not grow without
  // bound. The oldest goal is dropped first.
  static constexpr std::size_t kMaxTrackedGoals = 256;

  std::string name;
  std::mutex mutex;
  std::atomic<bool> running{false};
  bool owns_context = false;

  std::shared_ptr<rclcpp::Node> node;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
  std::thread spin_thread;

  std::map<SubscriptionHandle, Subscription> subscriptions;
  std::map<std::string, rclcpp::GenericPublisher::SharedPtr> publishers;
  std::map<std::string, rclcpp::GenericClient::SharedPtr> clients;
  std::map<std::string, Goal> goals;
  std::vector<std::string> goal_order;
  SubscriptionHandle next_handle = 1;

  // Creating a generic client costs a discovery round trip and an action is
  // addressed through three different services, so they are kept. `service` is
  // the key because that is what is actually being connected to.
  rclcpp::GenericClient::SharedPtr client(const std::string & service,
                                          const std::string & type) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = clients.find(service);
    if (it != clients.end()) {
      return it->second;
    }
    auto created = node->create_generic_client(service, type, rclcpp::ServicesQoS());
    if (created) {
      clients.emplace(service, created);
    }
    return created;
  }

  // Sending to a service that is not discovered yet does not fail - it hangs:
  // `async_send_request` hands back a future that is never completed, and the
  // caller waits out its whole timeout for a server that was there all along.
  // Measured against a Jazzy Fibonacci server, which is why every action call
  // waits for this first.
  bool waitForService(const rclcpp::GenericClient::SharedPtr & target, int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (target->service_is_ready()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return target->service_is_ready();
  }

  bool findGoal(const std::string & goal_id, Goal & goal_out) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = goals.find(goal_id);
    if (it == goals.end()) {
      return false;
    }
    goal_out = it->second;
    return true;
  }

  void rememberGoal(const std::string & goal_id, Goal goal) {
    std::lock_guard<std::mutex> lock(mutex);
    goals[goal_id] = std::move(goal);
    goal_order.push_back(goal_id);
    while (goal_order.size() > kMaxTrackedGoals) {
      goals.erase(goal_order.front());
      goal_order.erase(goal_order.begin());
    }
  }

};

RclcppBackend::RclcppBackend(RclcppBackendOptions options)
    : impl_(new Impl()) {
  impl_->name = options.node_namespace.empty()
                    ? options.node_name
                    : options.node_namespace + "/" + options.node_name;
  if (!options.ros_args.empty()) {
    // Stored for documentation purposes; argument parsing is delegated to the
    // embedding application to avoid stealing its argv.
    ROS2_GRPC_SDK_DEBUG("rclcpp backend node=", impl_->name,
                        " ros_args=", options.ros_args);
  }
  // Note: QoS depth and executor threading are read back in init() through a
  // captured copy.
  impl_->owns_context = !rclcpp::ok();
}

RclcppBackend::~RclcppBackend() { shutdown(); }

std::string RclcppBackend::rosDistro() const { return currentRosDistro(); }

Status RclcppBackend::init() {
  if (impl_->running.load(std::memory_order_acquire)) {
    return Status::success();
  }

  if (!rclcpp::ok()) {
    rclcpp::init(0, nullptr);
    impl_->owns_context = true;
  }

  try {
    impl_->node = std::make_shared<rclcpp::Node>(impl_->name);
    impl_->executor =
        std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    impl_->executor->add_node(impl_->node);
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("failed to create ROS2 node: ") +
                             error.what());
  }

  impl_->running.store(true, std::memory_order_release);
  impl_->spin_thread = std::thread([this]() { impl_->executor->spin(); });

  ROS2_GRPC_SDK_INFO("rclcpp backend initialised (distro=", rosDistro(),
                     ", node=", impl_->name, ")");
  return Status::success();
}

void RclcppBackend::shutdown() {
  if (!impl_->running.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->subscriptions.clear();
    impl_->publishers.clear();
    impl_->clients.clear();
    impl_->goals.clear();
    impl_->goal_order.clear();
  }
  if (impl_->executor) {
    impl_->executor->cancel();
  }
  if (impl_->spin_thread.joinable()) {
    impl_->spin_thread.join();
  }
  if (impl_->node && impl_->executor) {
    impl_->executor->remove_node(impl_->node);
  }
  impl_->node.reset();
  impl_->executor.reset();
  if (impl_->owns_context && rclcpp::ok()) {
    rclcpp::shutdown();
  }
  ROS2_GRPC_SDK_INFO("rclcpp backend shut down");
}

Status RclcppBackend::listTopics(std::vector<TopicInfo>& topics_out) {
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }
  topics_out.clear();
  try {
    const auto names_and_types = impl_->node->get_topic_names_and_types();
    topics_out.reserve(names_and_types.size());
    for (const auto& entry : names_and_types) {
      TopicInfo info;
      info.name = entry.first;
      if (!entry.second.empty()) {
        info.type = entry.second.front();
      }
      info.publisher_count =
          static_cast<int>(impl_->node->count_publishers(entry.first));
      info.subscriber_count =
          static_cast<int>(impl_->node->count_subscribers(entry.first));
      topics_out.push_back(std::move(info));
    }
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("get_topic_names_and_types failed: ") +
                             error.what());
  }
  return Status::success();
}

Status RclcppBackend::listServices(std::vector<ServiceInfo>& services_out) {
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }
  services_out.clear();
  try {
    const auto names_and_types = impl_->node->get_service_names_and_types();
    services_out.reserve(names_and_types.size());
    for (const auto& entry : names_and_types) {
      ServiceInfo info;
      info.name = entry.first;
      if (!entry.second.empty()) {
        info.type = entry.second.front();
      }
      // Everything this backend can see is on this machine; the "otherwise the
      // peer it was announced by" case in the struct is for backends that
      // aggregate several graphs.
      info.source = "local";
      services_out.push_back(std::move(info));
    }
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("get_service_names_and_types failed: ") +
                             error.what());
  }
  return Status::success();
}

Status RclcppBackend::listActions(std::vector<ActionInfo>& actions_out) {
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }
  actions_out.clear();
  try {
    for (const auto& entry : impl_->node->get_service_names_and_types()) {
      std::string action_name;
      if (!actionNameFromSendGoal(entry.first, action_name)) {
        continue;
      }
      ActionInfo info;
      info.name = std::move(action_name);
      if (!entry.second.empty()) {
        // A missing type is not fatal: the caller can still address the action,
        // it just cannot encode a goal for it. Reporting it with an empty type
        // beats hiding an action that is really there.
        actionTypeFromSendGoalType(entry.second.front(), info.type);
      }
      info.source = "local";
      actions_out.push_back(std::move(info));
    }
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("get_service_names_and_types failed: ") +
                             error.what());
  }
  return Status::success();
}

Status RclcppBackend::callService(const std::string& name,
                                  const std::string& type,
                                  const Bytes& request,
                                  const std::string& encoding, int timeout_ms,
                                  ServiceCallResult& result_out) {
  result_out = ServiceCallResult();
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }
  if (!encoding.empty() && encoding != "cdr") {
    return Status::error(StatusCode::InvalidArgument,
                         "rclcpp backend only supports the 'cdr' encoding");
  }

  const auto began = std::chrono::steady_clock::now();
  try {
    std::string service_type = type;
    if (service_type.empty()) {
      for (const auto& entry : impl_->node->get_service_names_and_types()) {
        if (entry.first == name && !entry.second.empty()) {
          service_type = entry.second.front();
          break;
        }
      }
      if (service_type.empty()) {
        return Status::error(StatusCode::NotFound,
                             "no service named " + name + " in this graph");
      }
    }

    ServiceTypeSupport types(service_type);
    MessageInstance request_message(introspectionMembers(types.request));
    {
      // A non-owning view over the caller's bytes: rmw_deserialize only reads.
      rmw_serialized_message_t serialized =
          rmw_get_zero_initialized_serialized_message();
      serialized.buffer = const_cast<std::uint8_t *>(request.data());
      serialized.buffer_length = request.size();
      serialized.buffer_capacity = request.size();
      serialized.allocator = rcutils_get_default_allocator();
      const rmw_ret_t rc =
          rmw_deserialize(&serialized, types.request, request_message.data);
      if (rc != RMW_RET_OK) {
        return Status::error(StatusCode::InvalidArgument,
                             "request does not decode as " + service_type);
      }
    }

    auto client = impl_->node->create_generic_client(
        name, service_type, rclcpp::ServicesQoS());
    auto future = client->async_send_request(request_message.data);
    const int wait_ms = timeout_ms > 0 ? timeout_ms : 5000;
    if (future.wait_for(std::chrono::milliseconds(wait_ms)) !=
        std::future_status::ready) {
      return Status::error(
          StatusCode::Timeout,
          "no response from " + name + " within " +
              std::to_string(wait_ms) + " ms");
    }

    auto response = future.get();
    if (!response) {
      return Status::error(StatusCode::Internal,
                           name + " returned an empty response");
    }

    rmw_serialized_message_t serialized =
        rmw_get_zero_initialized_serialized_message();
    // A zero-initialised serialized message carries a *zero* allocator, and
    // `rmw_serialize` grows its buffer through that allocator - so without this
    // the call dies with "unable to dynamically resize serialized message"
    // (measured against ROS2 Jazzy) and the answer is dropped *after* the
    // service has already run. The deserialize path above sets the same
    // allocator on its non-owning view, which is why only this direction broke.
    const rcutils_allocator_t allocator = rcutils_get_default_allocator();
    if (rmw_serialized_message_init(&serialized, 0u, &allocator) != RMW_RET_OK) {
      return Status::error(StatusCode::Internal,
                           "could not allocate the serialized response of " + name);
    }
    const rmw_ret_t rc =
        rmw_serialize(response.get(), types.response, &serialized);
    if (rc != RMW_RET_OK) {
      // rmw_serialize explains itself through rcutils' thread local error
      // string; without it the only thing a caller learns is "it did not work".
      const std::string detail = rcutils_get_error_string().str;
      rcutils_reset_error();
      rmw_serialized_message_fini(&serialized);
      return Status::error(StatusCode::Internal,
                           "could not serialise the response of " + name + ": " + detail);
    }
    result_out.response.assign(serialized.buffer,
                               serialized.buffer + serialized.buffer_length);
    rmw_serialized_message_fini(&serialized);
    result_out.success = true;
    result_out.encoding = "cdr";
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("callService failed: ") + error.what());
  }

  result_out.elapsed_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - began)
          .count();
  return Status::success();
}

// Resolves the action's own type from its name. `type` is a hint: the caller
// usually knows it, but `ros2 action list -t` style callers may not, and the
// graph can always be asked - `<action>/_action/send_goal` is advertised with
// the `<pkg>/action/<Name>_SendGoal` type, and stripping the suffix is exact.
std::string RclcppBackend::resolveActionType(const std::string& name,
                                             const std::string& type) {
  if (!type.empty()) {
    return type;
  }
  const std::string service = name + "/_action/send_goal";
  for (const auto& entry : impl_->node->get_service_names_and_types()) {
    if (entry.first != service || entry.second.empty()) {
      continue;
    }
    std::string action_type;
    if (actionTypeFromSendGoalType(entry.second.front(), action_type)) {
      return action_type;
    }
    return entry.second.front();
  }
  return {};
}

Status RclcppBackend::sendActionGoal(const std::string& name,
                                     const std::string& type,
                                     const Bytes& goal,
                                     const std::string& encoding,
                                     int timeout_ms, std::string& goal_id_out) {
  goal_id_out.clear();
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }
  if (!encoding.empty() && encoding != "cdr") {
    return Status::error(StatusCode::InvalidArgument,
                         "rclcpp backend only supports the 'cdr' encoding");
  }

  try {
    const std::string action_type = resolveActionType(name, type);
    if (action_type.empty()) {
      return Status::error(StatusCode::NotFound,
                           "no action named " + name + " in this graph");
    }

    MessageTypeSupport goal_type(action_type + "_Goal");
    ServiceTypeSupport send_type(action_type + "_SendGoal");
    const auto* request_members = introspectionMembers(send_type.request);
    const auto* response_members = introspectionMembers(send_type.response);

    MessageInstance request(request_members);
    const std::array<std::uint8_t, 16> uuid = randomUuid();
    std::memcpy(at(request.data, memberOffset(request_members, {"goal_id", "uuid"})),
                uuid.data(), uuid.size());

    // The caller hands over the action's *goal* message, not the request: the
    // goal_id and the wrapper belong to the generated service. Decoding
    // straight into the request's `goal` field both validates the payload and
    // builds the request in one step.
    std::string detail;
    if (!deserializeInto(goal, goal_type.handle,
                         at(request.data, memberOffset(request_members, {"goal"})),
                         detail)) {
      return Status::error(StatusCode::InvalidArgument,
                           "goal does not decode as " + action_type +
                               "_Goal: " + detail);
    }

    const int wait_ms = timeout_ms > 0 ? timeout_ms : 5000;
    auto client = impl_->client(name + "/_action/send_goal",
                                action_type + "_SendGoal");
    if (!client) {
      return Status::error(StatusCode::Internal,
                           "could not create a client for " + name);
    }
    if (!impl_->waitForService(client, wait_ms)) {
      return Status::error(StatusCode::Unavailable,
                           "no action server for " + name + " within " +
                               std::to_string(wait_ms) + " ms");
    }

    auto future = client->async_send_request(request.data);
    if (future.wait_for(std::chrono::milliseconds(wait_ms)) !=
        std::future_status::ready) {
      return Status::error(StatusCode::Timeout,
                           "no answer to the goal of " + name + " within " +
                               std::to_string(wait_ms) + " ms");
    }
    auto response = future.get();
    if (!response) {
      return Status::error(StatusCode::Internal,
                           name + " returned an empty SendGoal response");
    }
    const bool accepted = *reinterpret_cast<const bool*>(
        at(response.get(), memberOffset(response_members, {"accepted"})));
    if (!accepted) {
      // The server was reached and said no. That is an answer, not a failure,
      // and an empty goal id is how it is carried back: there is no goal to
      // follow up on, so inventing an id would make the next three RPCs fail
      // one by one instead.
      ROS2_GRPC_SDK_DEBUG("action '", name, "' refused the goal");
      return Status::success();
    }

    const std::string goal_id = bytesToHex(uuid.data(), uuid.size());
    Impl::Goal state;
    state.name = name;
    state.type = action_type;
    state.uuid = uuid;
    impl_->rememberGoal(goal_id, std::move(state));
    goal_id_out = goal_id;
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("sendActionGoal failed: ") + error.what());
  }
  return Status::success();
}

Status RclcppBackend::getActionResult(const std::string& name,
                                      const std::string& goal_id, int timeout_ms,
                                      ActionFeedback& result_out) {
  result_out = ActionFeedback();
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }

  Impl::Goal goal;
  if (!impl_->findGoal(goal_id, goal)) {
    return Status::error(StatusCode::NotFound, "no goal '" + goal_id + "'");
  }

  try {
    MessageTypeSupport result_type(goal.type + "_Result");
    ServiceTypeSupport get_type(goal.type + "_GetResult");
    const auto* request_members = introspectionMembers(get_type.request);
    const auto* response_members = introspectionMembers(get_type.response);

    MessageInstance request(request_members);
    std::uint8_t uuid[16];
    if (!hexToBytes(goal_id, uuid, sizeof(uuid))) {
      return Status::error(StatusCode::InvalidArgument,
                           "'" + goal_id + "' is not a goal id");
    }
    std::memcpy(at(request.data, memberOffset(request_members, {"goal_id", "uuid"})),
                uuid, sizeof(uuid));

    auto client = impl_->client(goal.name + "/_action/get_result",
                                goal.type + "_GetResult");
    if (!client) {
      return Status::error(StatusCode::Internal,
                           "could not create a client for " + goal.name);
    }
    if (!impl_->waitForService(client, 5000)) {
      return Status::error(StatusCode::Unavailable,
                           "no action server for " + goal.name);
    }

    // A stock action server holds this request until the goal is over, so a
    // non-positive timeout means "wait for the goal" rather than "give up
    // immediately" - the same reading `ros2 action send_goal` gives it.
    auto future = client->async_send_request(request.data);
    if (timeout_ms > 0) {
      if (future.wait_for(std::chrono::milliseconds(timeout_ms)) !=
          std::future_status::ready) {
        return Status::error(StatusCode::Timeout,
                             "goal " + goal_id + " of " + goal.name +
                                 " is still running after " +
                                 std::to_string(timeout_ms) + " ms");
      }
    } else {
      future.wait();
    }

    auto response = future.get();
    if (!response) {
      return Status::error(StatusCode::Internal,
                           goal.name + " returned an empty GetResult response");
    }
    const auto raw_status = *reinterpret_cast<const std::int8_t*>(
        at(response.get(), memberOffset(response_members, {"status"})));

    std::string detail;
    Bytes payload;
    if (!serializeAt(response.get(), memberOffset(response_members, {"result"}),
                     result_type.handle, payload, detail)) {
      return Status::error(StatusCode::Internal,
                           "could not serialise the result of " + goal.name +
                               ": " + detail);
    }

    result_out.name = goal.name;
    result_out.goal_id = goal_id;
    result_out.status = static_cast<GoalStatus>(raw_status);
    result_out.result = std::move(payload);
    result_out.encoding = "cdr";
    if (result_out.status == GoalStatus::Aborted) {
      result_out.error = "the action server aborted the goal";
    } else if (result_out.status == GoalStatus::Canceled) {
      result_out.error = "the goal was canceled";
    }
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("getActionResult failed: ") + error.what());
  }
  return Status::success();
}

bool RclcppBackend::cancelActionGoal(const std::string& name,
                                     const std::string& goal_id) {
  if (!impl_->node) {
    return false;
  }
  Impl::Goal goal;
  if (!impl_->findGoal(goal_id, goal)) {
    return false;
  }

  try {
    ServiceTypeSupport cancel_type("action_msgs/srv/CancelGoal");
    const auto* request_members = introspectionMembers(cancel_type.request);
    const auto* response_members = introspectionMembers(cancel_type.response);

    MessageInstance request(request_members);
    std::uint8_t uuid[16];
    if (!hexToBytes(goal_id, uuid, sizeof(uuid))) {
      return false;
    }
    std::memcpy(
        at(request.data, memberOffset(request_members, {"goal_info", "goal_id", "uuid"})),
        uuid, sizeof(uuid));

    auto client = impl_->client(goal.name + "/_action/cancel_goal",
                                "action_msgs/srv/CancelGoal");
    if (!client || !impl_->waitForService(client, 2000)) {
      return false;
    }
    auto future = client->async_send_request(request.data);
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
      ROS2_GRPC_SDK_DEBUG("action '", name, "': cancel request timed out");
      return false;
    }
    auto response = future.get();
    if (!response) {
      return false;
    }
    // ERROR_NONE means "the goal is being canceled" and ERROR_GOAL_TERMINATED
    // that it had already finished. Both leave nothing running, but only the
    // first answers the question that was asked.
    const auto code = *reinterpret_cast<const std::int8_t*>(
        at(response.get(), memberOffset(response_members, {"return_code"})));
    // The goal stays in the table: a cancel is a request, and the goal is not
    // necessarily over yet. `streamActionFeedback` still has a terminal message
    // to deliver, and it gets the result that goes with it from here - dropping
    // the goal on cancel would turn a canceled goal into a NotFound halfway
    // through its own stream. The table's size bound is what keeps it small.
    return code == 0;
  } catch (const std::exception& error) {
    ROS2_GRPC_SDK_DEBUG("cancelActionGoal('", name, "') failed: ", error.what());
    return false;
  }
}

Status RclcppBackend::streamActionFeedback(const std::string& name,
                                           const std::string& goal_id,
                                           const ActionFeedbackSink& sink,
                                           const std::function<bool()>& should_stop) {
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }
  Impl::Goal goal;
  if (!impl_->findGoal(goal_id, goal)) {
    return Status::error(StatusCode::NotFound, "no goal '" + goal_id + "'");
  }

  try {
    MessageTypeSupport message_type(goal.type + "_FeedbackMessage");
    MessageTypeSupport feedback_type(goal.type + "_Feedback");
    MessageTypeSupport status_type("action_msgs/msg/GoalStatusArray");

    const auto* message_members = introspectionMembers(message_type.handle);
    const auto* array_members = introspectionMembers(status_type.handle);
    const auto* goal_status_members = memberMembers(array_members, "status_list");

    const std::uint32_t feedback_offset = memberOffset(message_members, {"feedback"});
    const std::uint32_t message_goal_offset =
        memberOffset(message_members, {"goal_id", "uuid"});
    const std::uint32_t status_goal_offset =
        memberOffset(goal_status_members, {"goal_info", "goal_id", "uuid"});
    const std::uint32_t status_offset = memberOffset(goal_status_members, {"status"});

    std::uint8_t wanted[16];
    if (!hexToBytes(goal_id, wanted, sizeof(wanted))) {
      return Status::error(StatusCode::InvalidArgument,
                           "'" + goal_id + "' is not a goal id");
    }

    std::mutex queue_mutex;
    std::vector<Bytes> queue;
    std::atomic<bool> terminal{false};

    // Both callbacks run on the executor thread. Feedback is queued rather than
    // handed to the sink from there, because the sink writes to a gRPC stream:
    // a slow reader has to slow down its own goal, not every other subscriber
    // in the process. The typesupport libraries are captured so they outlive
    // the local handles even if a callback is still in flight.
    auto feedback_subscription = impl_->node->create_generic_subscription(
        name + "/_action/feedback", goal.type + "_FeedbackMessage",
        rclcpp::QoS(10),
        [&, library = message_type.library, handle = message_type.handle,
         message_members](std::shared_ptr<rclcpp::SerializedMessage> message) {
          MessageInstance instance(message_members);
          const rcl_serialized_message_t& raw = message->get_rcl_serialized_message();
          rmw_serialized_message_t view = rmw_get_zero_initialized_serialized_message();
          view.buffer = raw.buffer;
          view.buffer_length = raw.buffer_length;
          view.buffer_capacity = raw.buffer_length;
          view.allocator = rcutils_get_default_allocator();
          if (rmw_deserialize(&view, handle, instance.data) != RMW_RET_OK) {
            rcutils_reset_error();
            return;
          }
          if (std::memcmp(at(instance.data, message_goal_offset), wanted, 16) != 0) {
            return;  // another goal of the same action
          }
          Bytes payload;
          std::string detail;
          if (!serializeAt(instance.data, feedback_offset, feedback_type.handle,
                           payload, detail)) {
            return;
          }
          std::lock_guard<std::mutex> lock(queue_mutex);
          queue.push_back(std::move(payload));
        });

    // The status topic is how a goal is known to be over. Asking the result
    // service instead would work, but a stock server keeps one result request
    // per goal pending, so a caller that also uses GetActionResult would have
    // its own request swallowed.
    auto status_subscription = impl_->node->create_generic_subscription(
        name + "/_action/status", "action_msgs/msg/GoalStatusArray",
        rclcpp::QoS(10),
        [&, library = status_type.library, handle = status_type.handle,
         array_members, status_goal_offset, status_offset](
          std::shared_ptr<rclcpp::SerializedMessage> message) {
          MessageInstance instance(array_members);
          const rcl_serialized_message_t& raw = message->get_rcl_serialized_message();
          rmw_serialized_message_t view = rmw_get_zero_initialized_serialized_message();
          view.buffer = raw.buffer;
          view.buffer_length = raw.buffer_length;
          view.buffer_capacity = raw.buffer_length;
          view.allocator = rcutils_get_default_allocator();
          if (rmw_deserialize(&view, handle, instance.data) != RMW_RET_OK) {
            rcutils_reset_error();
            return;
          }
          const auto* list = findMember(array_members, "status_list");
          const void* list_ptr = at(instance.data, list->offset_);
          const std::size_t count = list->size_function(list_ptr);
          for (std::size_t i = 0; i < count; ++i) {
            const void* entry = list->get_const_function(list_ptr, i);
            if (std::memcmp(at(entry, status_goal_offset), wanted, 16) != 0) {
              continue;
            }
            const auto raw_status =
                *reinterpret_cast<const std::int8_t*>(at(entry, status_offset));
            if (isTerminal(static_cast<GoalStatus>(raw_status))) {
              terminal.store(true, std::memory_order_release);
            }
          }
        });

    // Polled rather than awaited on a condition variable: `should_stop` has to
    // be honoured when the goal is silent, which is most of the time.
    const auto terminal_grace = std::chrono::milliseconds(150);
    std::chrono::steady_clock::time_point terminal_seen{};
    bool stopped_by_caller = false;
    while (true) {
      std::vector<Bytes> batch;
      {
        std::lock_guard<std::mutex> lock(queue_mutex);
        batch.swap(queue);
      }
      for (const Bytes& payload : batch) {
        ActionFeedback item;
        item.name = name;
        item.goal_id = goal_id;
        item.feedback = payload;
        item.encoding = "cdr";
        item.status = GoalStatus::Executing;
        if (sink && !sink(item)) {
          feedback_subscription.reset();
          status_subscription.reset();
          return Status::success();
        }
      }

      if (terminal.load(std::memory_order_acquire)) {
        // The last feedback and the terminal status are published together, so
        // give the queue a moment to catch up before declaring the goal over.
        const auto now = std::chrono::steady_clock::now();
        if (terminal_seen == std::chrono::steady_clock::time_point{}) {
          terminal_seen = now;
        } else if (now - terminal_seen >= terminal_grace) {
          break;
        }
      } else if (should_stop && should_stop()) {
        stopped_by_caller = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    feedback_subscription.reset();
    status_subscription.reset();

    if (stopped_by_caller) {
      return Status::success();
    }

    // The goal is over, so the result is there to collect, and pairing it with
    // the terminal status is what lets the bridge republish a whole outcome on
    // `<name>/_result` from this one stream.
    ActionFeedback last;
    const Status status = getActionResult(name, goal_id, 5000, last);
    if (!status.ok()) {
      return status;
    }
    if (sink) {
      sink(last);
    }
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("streamActionFeedback failed: ") + error.what());
  }
  return Status::success();
}

Status RclcppBackend::publish(const std::string& topic,
                              const std::string& type, const Bytes& data,
                              const std::string& encoding) {
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }
  if (!encoding.empty() && encoding != "cdr") {
    return Status::error(StatusCode::InvalidArgument,
                         "rclcpp backend only supports the 'cdr' encoding");
  }

  try {
    rclcpp::GenericPublisher::SharedPtr publisher;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      auto it = impl_->publishers.find(topic);
      if (it == impl_->publishers.end()) {
        // Jazzy has no `GenericPublisher::create`: the type-erased publisher
        // is made by the node, which is also what loads the type support
        // library for `type` by name. Throws when the package is missing from
        // AMENT_PREFIX_PATH, which is what the catch below reports.
        publisher = impl_->node->create_generic_publisher(
            topic, type, rclcpp::QoS(10));
        if (!publisher) {
          return Status::error(StatusCode::NotFound,
                              "no type support available for " + type);
        }
        impl_->publishers.emplace(topic, publisher);
      } else {
        publisher = it->second;
      }
    }
    publisher->publish(toSerialized(data));
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("publish failed: ") + error.what());
  }
  return Status::success();
}

Status RclcppBackend::subscribe(const std::string& topic,
                                const std::string& type, MessageSink sink,
                                SubscriptionHandle& handle_out,
                                int throttle_ms) {
  if (!impl_->node) {
    return Status::error(StatusCode::Unavailable, "rclcpp backend not started");
  }

  try {
    auto callback = [sink = std::move(sink), topic, type,
                     throttle_ms](std::shared_ptr<rclcpp::SerializedMessage> msg) {
      static thread_local std::map<std::string, std::int64_t> last_sent;
      if (throttle_ms > 0) {
        const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        auto& last = last_sent[topic];
        if (last != 0 && now - last < static_cast<std::int64_t>(throttle_ms) * 1000000) {
          return;
        }
        last = now;
      }
      Message message;
      message.topic = topic;
      message.type = type;
      message.encoding = "cdr";
      message.stamp_ns =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
      if (msg) {
        message.data = toBytes(*msg);
      }
      if (sink) {
        sink(std::move(message));
      }
    };

    auto subscription = impl_->node->create_generic_subscription(
        topic, type, rclcpp::QoS(10), std::move(callback));
    if (!subscription) {
      return Status::error(StatusCode::NotFound,
                          "no type support available for " + type);
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::Subscription entry;
    entry.subscription = subscription;
    entry.type = type;
    handle_out = impl_->next_handle++;
    impl_->subscriptions.emplace(handle_out, std::move(entry));
  } catch (const std::exception& error) {
    return Status::error(StatusCode::Internal,
                         std::string("subscribe failed: ") + error.what());
  }
  return Status::success();
}

void RclcppBackend::unsubscribe(SubscriptionHandle handle) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->subscriptions.erase(handle);
}

}  // namespace ros2_grpc_sdk

#endif  // ROS2_GRPC_SDK_WITH_ROS2
