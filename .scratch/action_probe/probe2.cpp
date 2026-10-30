// Probe 2: prototype of the four action RPCs against a stock rclcpp_action
// server. Everything the SDK implementation needs to know is measured here:
//
//   1. `MessageMember::members_` carries the *introspection* identifier, and
//      `rmw_serialize` / `rmw_deserialize` reject it ("type support not from
//      this implementation"). Only `<action>_Goal` / `_Result` / `_Feedback`
//      loaded by name from `rosidl_typesupport_cpp` are usable - but their
//      introspection members are pointer-identical to the nested ones, so
//      offsets can come from the nested handle and codec from the by-name one.
//   2. Jazzy's `SendGoal_Response` is `{ bool accepted; builtin_interfaces/Time
//      stamp; }` - not the two-field struct older tutorials describe.
//   3. send_goal -> feedback -> get_result -> cancel_goal all round trip.
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/generic_client.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rclcpp/typesupport_helpers.hpp>

#include <rcutils/allocator.h>
#include <rcutils/error_handling.h>
#include <rmw/rmw.h>
#include <rosidl_runtime_c/message_type_support_struct.h>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace it = rosidl_typesupport_introspection_cpp;

static const it::MessageMembers * membersOf(const rosidl_message_type_support_t * ts)
{
  const auto * handle = get_message_typesupport_handle(ts, it::typesupport_identifier);
  return static_cast<const it::MessageMembers *>(handle->data);
}

static const it::MessageMember * findMember(const it::MessageMembers * m, const char * name)
{
  for (std::uint32_t i = 0; i < m->member_count_; ++i) {
    if (std::strcmp(m->members_[i].name_, name) == 0) { return &m->members_[i]; }
  }
  return nullptr;
}

// Walks a chain of message members, accumulating the byte offset, so a field
// nested two levels deep (CancelGoal's goal_info.goal_id.uuid) is one call.
static std::uint32_t offsetOfPath(
  const it::MessageMembers * m, const std::vector<const char *> & path)
{
  std::uint32_t offset = 0;
  const it::MessageMembers * current = m;
  for (const char * step : path) {
    const it::MessageMember * member = findMember(current, step);
    if (member == nullptr) { throw std::runtime_error(std::string("no member ") + step); }
    offset += member->offset_;
    if (member->members_ != nullptr) { current = membersOf(member->members_); }
  }
  return offset;
}

static const it::MessageMembers * membersAtPath(
  const it::MessageMembers * m, const std::vector<const char *> & path)
{
  const it::MessageMembers * current = m;
  for (const char * step : path) {
    const it::MessageMember * member = findMember(current, step);
    if (member == nullptr || member->members_ == nullptr) {
      throw std::runtime_error(std::string("no message member ") + step);
    }
    current = membersOf(member->members_);
  }
  return current;
}

static void dump(const char * label, const it::MessageMembers * m)
{
  std::cout << label << ": members=" << m->member_count_ << " size_of=" << m->size_of_ << "\n";
  for (std::uint32_t i = 0; i < m->member_count_; ++i) {
    const it::MessageMember & member = m->members_[i];
    std::cout << "    [" << i << "] " << member.name_ << " offset=" << member.offset_
              << " array=" << member.is_array_ << "/" << member.array_size_ << "\n";
  }
}

struct MessageInstance
{
  const it::MessageMembers * members;
  void * data;
  explicit MessageInstance(const it::MessageMembers * m)
  : members(m), data(new std::uint8_t[m->size_of_])
  {
    m->init_function(data, rosidl_runtime_cpp::MessageInitialization::ZERO);
  }
  ~MessageInstance()
  {
    members->fini_function(data);
    delete[] reinterpret_cast<std::uint8_t *>(data);
  }
  MessageInstance(const MessageInstance &) = delete;
  MessageInstance & operator=(const MessageInstance &) = delete;
};

static std::vector<std::uint8_t> serialize(
  const void * data, const rosidl_message_type_support_t * ts, bool & ok)
{
  ok = false;
  const rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_serialized_message_t buffer = rmw_get_zero_initialized_serialized_message();
  if (rmw_serialized_message_init(&buffer, 0u, &allocator) != RMW_RET_OK) { return {}; }
  if (rmw_serialize(data, ts, &buffer) != RMW_RET_OK) {
    std::cout << "      rmw_serialize error: " << rcutils_get_error_string().str << std::endl;
    rcutils_reset_error();
    rmw_serialized_message_fini(&buffer);
    return {};
  }
  std::vector<std::uint8_t> out(buffer.buffer, buffer.buffer + buffer.buffer_length);
  rmw_serialized_message_fini(&buffer);
  ok = true;
  return out;
}

static bool deserialize(const std::uint8_t * bytes, std::size_t size,
  const rosidl_message_type_support_t * ts, void * data)
{
  rmw_serialized_message_t view = rmw_get_zero_initialized_serialized_message();
  view.buffer = const_cast<std::uint8_t *>(bytes);
  view.buffer_length = size;
  view.buffer_capacity = size;
  view.allocator = rcutils_get_default_allocator();
  const rmw_ret_t rc = rmw_deserialize(&view, ts, data);
  if (rc != RMW_RET_OK) {
    std::cout << "      rmw_deserialize error: " << rcutils_get_error_string().str << std::endl;
    rcutils_reset_error();
  }
  return rc == RMW_RET_OK;
}

static std::string hex(const std::uint8_t * bytes, std::size_t size)
{
  static const char * digits = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(digits[bytes[i] >> 4]);
    out.push_back(digits[bytes[i] & 0xf]);
  }
  return out;
}

struct ActionCodec
{
  std::shared_ptr<rcpputils::SharedLibrary> goal_lib, result_lib, feedback_lib,
    feedback_message_lib, send_lib, get_result_lib, cancel_lib;
  const rosidl_message_type_support_t * goal = nullptr;
  const rosidl_message_type_support_t * result = nullptr;
  const rosidl_message_type_support_t * feedback = nullptr;
  const rosidl_message_type_support_t * feedback_message = nullptr;
  const rosidl_service_type_support_t * send_goal = nullptr;
  const rosidl_service_type_support_t * get_result = nullptr;
  const rosidl_service_type_support_t * cancel_goal = nullptr;

  explicit ActionCodec(const std::string & action)
  {
    goal_lib = rclcpp::get_typesupport_library(action + "_Goal", "rosidl_typesupport_cpp");
    goal = rclcpp::get_message_typesupport_handle(
      action + "_Goal", "rosidl_typesupport_cpp", *goal_lib);
    result_lib = rclcpp::get_typesupport_library(action + "_Result", "rosidl_typesupport_cpp");
    result = rclcpp::get_message_typesupport_handle(
      action + "_Result", "rosidl_typesupport_cpp", *result_lib);
    feedback_lib = rclcpp::get_typesupport_library(action + "_Feedback", "rosidl_typesupport_cpp");
    feedback = rclcpp::get_message_typesupport_handle(
      action + "_Feedback", "rosidl_typesupport_cpp", *feedback_lib);
    feedback_message_lib = rclcpp::get_typesupport_library(
      action + "_FeedbackMessage", "rosidl_typesupport_cpp");
    feedback_message = rclcpp::get_message_typesupport_handle(
      action + "_FeedbackMessage", "rosidl_typesupport_cpp", *feedback_message_lib);
    send_lib = rclcpp::get_typesupport_library(action + "_SendGoal", "rosidl_typesupport_cpp");
    send_goal = rclcpp::get_service_typesupport_handle(
      action + "_SendGoal", "rosidl_typesupport_cpp", *send_lib);
    get_result_lib = rclcpp::get_typesupport_library(action + "_GetResult", "rosidl_typesupport_cpp");
    get_result = rclcpp::get_service_typesupport_handle(
      action + "_GetResult", "rosidl_typesupport_cpp", *get_result_lib);
    cancel_lib = rclcpp::get_typesupport_library(
      "action_msgs/srv/CancelGoal", "rosidl_typesupport_cpp");
    cancel_goal = rclcpp::get_service_typesupport_handle(
      "action_msgs/srv/CancelGoal", "rosidl_typesupport_cpp", *cancel_lib);
  }
};

static void printCdr(const char * label, const std::vector<std::uint8_t> & bytes)
{
  std::cout << "  " << label << " (" << bytes.size() << " bytes):";
  for (std::uint8_t byte : bytes) { printf(" %02x", byte); }
  std::cout << std::endl;
}

static void waitReady(const rclcpp::GenericClient::SharedPtr & client, const char * what)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!client->service_is_ready() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "  " << what << " service_is_ready=" << (client->service_is_ready() ? "yes" : "no")
            << std::endl;
}

int main()
{
  const std::string action = "action_tutorials_interfaces/action/Fibonacci";
  rclcpp::init(0, nullptr);
  auto node = std::make_shared<rclcpp::Node>("action_probe2");
  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  executor->add_node(node);
  std::thread spinner([&executor]() {executor->spin();});

  int failures = 0;
  const std::uint8_t uuid[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  const std::uint8_t uuid2[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

  try {
    ActionCodec codec(action);
    const it::MessageMembers * req_members = membersOf(codec.send_goal->request_typesupport);
    const it::MessageMembers * resp_members = membersOf(codec.send_goal->response_typesupport);
    const it::MessageMembers * gr_req = membersOf(codec.get_result->request_typesupport);
    const it::MessageMembers * gr_resp = membersOf(codec.get_result->response_typesupport);
    const it::MessageMembers * cancel_req = membersOf(codec.cancel_goal->request_typesupport);
    const it::MessageMembers * cancel_resp = membersOf(codec.cancel_goal->response_typesupport);

    if (true) {
      dump("SendGoal_Request", req_members);
      dump("SendGoal_Response", resp_members);
      dump("GetResult_Response", gr_resp);
      dump("CancelGoal_Request", cancel_req);
      dump("CancelGoal_Response", cancel_resp);
      std::cout << "nested goal members == by-name goal members: "
                << (membersAtPath(req_members, {"goal"}) == membersOf(codec.goal) ? "yes" : "no")
                << std::endl;
    }

    // ---- SendGoal(order=5)
    std::string goal_id;
    {
      MessageInstance goal(membersOf(codec.goal));
      *reinterpret_cast<std::int32_t *>(
        static_cast<std::uint8_t *>(goal.data) + offsetOfPath(membersOf(codec.goal), {"order"})) = 5;

      MessageInstance request(req_members);
      std::memcpy(static_cast<std::uint8_t *>(request.data) +
        offsetOfPath(req_members, {"goal_id", "uuid"}), uuid, 16);
      const std::uint32_t goal_offset = offsetOfPath(req_members, {"goal"});
      // Deserialize the caller's CDR straight into the request's goal field.
      bool ok = false;
      const std::vector<std::uint8_t> goal_cdr = serialize(goal.data, codec.goal, ok);
      printCdr("goal cdr", goal_cdr);
      if (!ok || !deserialize(goal_cdr.data(), goal_cdr.size(), codec.goal,
          static_cast<std::uint8_t *>(request.data) + goal_offset))
      {
        std::cout << "building the SendGoal request FAILED" << std::endl;
        ++failures;
      }

      auto client = node->create_generic_client(
        "/fibonacci/_action/send_goal", action + "_SendGoal", rclcpp::ServicesQoS());
      waitReady(client, "send_goal");
      auto future = client->async_send_request(request.data);
      if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
        std::cout << "send_goal: TIMEOUT" << std::endl;
        ++failures;
      } else {
        auto response = future.get();
        const bool accepted = *reinterpret_cast<bool *>(
          static_cast<std::uint8_t *>(response.get()) +
          offsetOfPath(resp_members, {"accepted"}));
        std::cout << "send_goal: accepted=" << (accepted ? "true" : "false") << std::endl;
        if (!accepted) { ++failures; }
        goal_id = hex(uuid, 16);
      }
    }

    // ---- feedback stream
    std::atomic<int> feedback_count{0};
    {
      auto subscription = node->create_generic_subscription(
        "/fibonacci/_action/feedback", action + "_FeedbackMessage", rclcpp::QoS(10),
        [&](std::shared_ptr<rclcpp::SerializedMessage> message) {
          const rclcpp::SerializedMessage & local = *message;
          const rcl_serialized_message_t & raw = local.get_rcl_serialized_message();
          MessageInstance view(membersOf(codec.feedback_message));
          if (!deserialize(raw.buffer, raw.buffer_length, codec.feedback_message, view.data)) {
            return;
          }
          const std::uint8_t * id = static_cast<const std::uint8_t *>(view.data) +
          offsetOfPath(membersOf(codec.feedback_message), {"goal_id", "uuid"});
          if (std::memcmp(id, uuid, 16) != 0) { return; }
          bool ok = false;
          const std::vector<std::uint8_t> payload = serialize(
            static_cast<const std::uint8_t *>(view.data) +
            offsetOfPath(membersOf(codec.feedback_message), {"feedback"}), codec.feedback, ok);
          if (ok) {
            ++feedback_count;
            std::cout << "  feedback #" << feedback_count << " cdr (" << payload.size()
                      << " bytes)" << std::endl;
          }
        });
      std::this_thread::sleep_for(std::chrono::seconds(2));
      std::cout << "feedback messages seen for our goal: " << feedback_count.load() << std::endl;
      if (feedback_count.load() == 0) { ++failures; }
      subscription.reset();
    }

    // ---- status topic: the canonical way to learn that a goal is over. It
    //      does not consume the server's single pending result slot, which a
    //      blocking get_result would.
    std::atomic<int> status_entries{0};
    std::atomic<int> status_terminal{0};
    {
      auto status_lib = rclcpp::get_typesupport_library(
        "action_msgs/msg/GoalStatusArray", "rosidl_typesupport_cpp");
      const auto * status_ts = rclcpp::get_message_typesupport_handle(
        "action_msgs/msg/GoalStatusArray", "rosidl_typesupport_cpp", *status_lib);
      const it::MessageMembers * array_members = membersOf(status_ts);
      const it::MessageMembers * status_members = membersAtPath(array_members, {"status_list"});
      const it::MessageMembers * info_members = membersAtPath(status_members, {"goal_info"});
      dump("GoalStatusArray", array_members);
      dump("GoalStatus", status_members);
      dump("GoalInfo", info_members);

      auto subscription = node->create_generic_subscription(
        "/fibonacci/_action/status", "action_msgs/msg/GoalStatusArray", rclcpp::QoS(10),
        [&, array_members, status_members, info_members, status_ts](
          std::shared_ptr<rclcpp::SerializedMessage> message) {
          const rcl_serialized_message_t & raw = message->get_rcl_serialized_message();
          MessageInstance view(array_members);
          if (!deserialize(raw.buffer, raw.buffer_length, status_ts, view.data)) { return; }
          const it::MessageMember * list = findMember(array_members, "status_list");
          const void * list_ptr = static_cast<const std::uint8_t *>(view.data) + list->offset_;
          const std::size_t count = list->size_function(list_ptr);
          for (std::size_t i = 0; i < count; ++i) {
            const void * entry = list->get_const_function(list_ptr, i);
            const std::uint8_t * id = static_cast<const std::uint8_t *>(entry) +
              offsetOfPath(status_members, {"goal_info", "goal_id", "uuid"});
            const std::int8_t goal_status = *reinterpret_cast<const std::int8_t *>(
              static_cast<const std::uint8_t *>(entry) +
              offsetOfPath(status_members, {"status"}));
            ++status_entries;
            if (std::memcmp(id, uuid, 16) == 0) {
              std::cout << "  status[" << i << "] goal=" << hex(id, 16)
                        << " status=" << static_cast<int>(goal_status) << std::endl;
              if (goal_status >= 4) { ++status_terminal; }
            }
          }
        });
      std::this_thread::sleep_for(std::chrono::seconds(3));
      std::cout << "status array entries seen: " << status_entries.load()
                << ", terminal for our goal: " << status_terminal.load() << std::endl;
      if (status_entries.load() == 0) { ++failures; }
      subscription.reset();
    }

    // ---- GetResult
    {
      MessageInstance request(gr_req);
      std::memcpy(static_cast<std::uint8_t *>(request.data) +
        offsetOfPath(gr_req, {"goal_id", "uuid"}), uuid, 16);
      auto client = node->create_generic_client(
        "/fibonacci/_action/get_result", action + "_GetResult", rclcpp::ServicesQoS());
      waitReady(client, "get_result");
      auto future = client->async_send_request(request.data);
      if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
        std::cout << "get_result: TIMEOUT" << std::endl;
        ++failures;
      } else {
        auto response = future.get();
        const std::int8_t status = *reinterpret_cast<std::int8_t *>(
          static_cast<std::uint8_t *>(response.get()) + offsetOfPath(gr_resp, {"status"}));
        std::cout << "get_result: status=" << static_cast<int>(status) << " (4=succeeded)"
                  << std::endl;
        if (status != 4) { ++failures; }
        bool ok = false;
        const std::vector<std::uint8_t> result_cdr = serialize(
          static_cast<std::uint8_t *>(response.get()) + offsetOfPath(gr_resp, {"result"}),
          codec.result, ok);
        printCdr("result cdr", result_cdr);
        MessageInstance decoded(membersOf(codec.result));
        if (!deserialize(result_cdr.data(), result_cdr.size(), codec.result, decoded.data)) {
          ++failures;
        } else {
          const it::MessageMember * sequence = findMember(membersOf(codec.result), "sequence");
          const void * sequence_ptr = static_cast<const std::uint8_t *>(decoded.data) +
          sequence->offset_;
          const std::size_t count = sequence->size_function(sequence_ptr);
          std::cout << "  decoded sequence[" << count << "] =";
          for (std::size_t i = 0; i < count; ++i) {
            std::cout << " " << *static_cast<const std::int32_t *>(
              sequence->get_const_function(sequence_ptr, i));
          }
          std::cout << std::endl;
        }
      }
    }

    // ---- a second goal that gets cancelled
    {
      MessageInstance goal(membersOf(codec.goal));
      *reinterpret_cast<std::int32_t *>(
        static_cast<std::uint8_t *>(goal.data) + offsetOfPath(membersOf(codec.goal), {"order"})) = 40;
      bool ok = false;
      const std::vector<std::uint8_t> goal_cdr = serialize(goal.data, codec.goal, ok);
      MessageInstance request(req_members);
      std::memcpy(static_cast<std::uint8_t *>(request.data) +
        offsetOfPath(req_members, {"goal_id", "uuid"}), uuid2, 16);
      if (!deserialize(goal_cdr.data(), goal_cdr.size(), codec.goal,
          static_cast<std::uint8_t *>(request.data) + offsetOfPath(req_members, {"goal"})))
      {
        ++failures;
      }
      auto client = node->create_generic_client(
        "/fibonacci/_action/send_goal", action + "_SendGoal", rclcpp::ServicesQoS());
      waitReady(client, "send_goal(2)");
      auto future = client->async_send_request(request.data);
      if (future.wait_for(std::chrono::seconds(15)) == std::future_status::ready) {
        const bool accepted = *reinterpret_cast<bool *>(
          static_cast<std::uint8_t *>(future.get().get()) +
          offsetOfPath(resp_members, {"accepted"}));
        std::cout << "send_goal(2): accepted=" << (accepted ? "true" : "false") << std::endl;

        MessageInstance cancel(cancel_req);
        std::memcpy(static_cast<std::uint8_t *>(cancel.data) +
          offsetOfPath(cancel_req, {"goal_info", "goal_id", "uuid"}), uuid2, 16);
        auto cancel_client = node->create_generic_client(
          "/fibonacci/_action/cancel_goal", "action_msgs/srv/CancelGoal", rclcpp::ServicesQoS());
        waitReady(cancel_client, "cancel_goal");
        auto cancel_future = cancel_client->async_send_request(cancel.data);
        if (cancel_future.wait_for(std::chrono::seconds(15)) == std::future_status::ready) {
          const std::int8_t code = *reinterpret_cast<std::int8_t *>(
            static_cast<std::uint8_t *>(cancel_future.get().get()) +
            offsetOfPath(cancel_resp, {"return_code"}));
          std::cout << "cancel_goal: return_code=" << static_cast<int>(code)
                    << " (0=accepted, 2=goal finished)" << std::endl;
          if (code != 0) { ++failures; }
        } else {
          std::cout << "cancel_goal: TIMEOUT" << std::endl;
          ++failures;
        }
      } else {
        std::cout << "send_goal(2): TIMEOUT" << std::endl;
        ++failures;
      }
    }
  } catch (const std::exception & error) {
    std::cout << "EXCEPTION: " << error.what() << std::endl;
    ++failures;
  }

  executor->cancel();
  spinner.join();
  rclcpp::shutdown();
  std::cout << "failures=" << failures << std::endl;
  return failures == 0 ? 0 : 1;
}
