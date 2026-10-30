// The four action RPCs against a real `rclcpp_action` server.
//
// The server is started by this test rather than borrowed from the machine, so
// the test is hermetic: it needs ROS2 and `action_tutorials_interfaces`, but no
// peer, no simulator and no running agent. What it pins down is the whole
// surface the bridge relies on - list, send, follow, result, cancel - including
// the two shapes that are easy to get wrong:
//   * a goal the server *refuses* comes back as an empty goal id, not as an
//     error, because the transport worked and the answer was "no";
//   * the terminal message of the feedback stream carries the result, which is
//     what lets the bridge republish one outcome on `<name>/_result`.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <action_tutorials_interfaces/action/fibonacci.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "ros2_grpc_sdk/server/rclcpp_backend.hpp"

namespace sdk = ros2_grpc_sdk;
using Fibonacci = action_tutorials_interfaces::action::Fibonacci;
using GoalHandle = rclcpp_action::ServerGoalHandle<Fibonacci>;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

namespace {

// A Fibonacci action server that behaves like the stock one, plus a knob to
// refuse every goal so the refusal path can be exercised.
class FibonacciServer : public rclcpp::Node {
 public:
  FibonacciServer(const std::string& node_name, const std::string& action_name,
                  bool refuse)
      : Node(node_name), refuse_(refuse) {
    using std::placeholders::_1;
    using std::placeholders::_2;
    server_ = rclcpp_action::create_server<Fibonacci>(
        this, action_name,
        std::bind(&FibonacciServer::onGoal, this, _1, _2),
        std::bind(&FibonacciServer::onCancel, this, _1),
        std::bind(&FibonacciServer::onAccepted, this, _1));
  }

 private:
  rclcpp_action::GoalResponse onGoal(const rclcpp_action::GoalUUID&,
                                     std::shared_ptr<const Fibonacci::Goal> goal) {
    if (refuse_ || goal->order <= 0) {
      return rclcpp_action::GoalResponse::REJECT;
    }
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse onCancel(const std::shared_ptr<GoalHandle>) {
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void onAccepted(const std::shared_ptr<GoalHandle> handle) {
    std::thread([this, handle]() { execute(handle); }).detach();
  }

  void execute(const std::shared_ptr<GoalHandle> handle) {
    auto feedback = std::make_shared<Fibonacci::Feedback>();
    feedback->partial_sequence = {0, 1};
    auto result = std::make_shared<Fibonacci::Result>();
    rclcpp::Rate rate(20);
    for (int i = 1; i < handle->get_goal()->order; ++i) {
      if (handle->is_canceling()) {
        result->sequence = feedback->partial_sequence;
        handle->canceled(result);
        return;
      }
      feedback->partial_sequence.push_back(
          feedback->partial_sequence[i] + feedback->partial_sequence[i - 1]);
      handle->publish_feedback(feedback);
      rate.sleep();
    }
    result->sequence = feedback->partial_sequence;
    handle->succeed(result);
  }

  bool refuse_;
  rclcpp_action::Server<Fibonacci>::SharedPtr server_;
};

sdk::Bytes serializeGoal(const Fibonacci::Goal& goal) {
  rclcpp::Serialization<Fibonacci::Goal> serializer;
  rclcpp::SerializedMessage raw;
  serializer.serialize_message(&goal, &raw);
  const rcl_serialized_message_t& view = raw.get_rcl_serialized_message();
  return sdk::Bytes(view.buffer, view.buffer + view.buffer_length);
}

template <typename Message>
bool deserialize(const sdk::Bytes& bytes, Message& out) {
  rclcpp::Serialization<Message> serializer;
  rclcpp::SerializedMessage raw(bytes.size());
  rcl_serialized_message_t& view = raw.get_rcl_serialized_message();
  if (!bytes.empty()) {
    std::memcpy(view.buffer, bytes.data(), bytes.size());
  }
  view.buffer_length = bytes.size();
  try {
    serializer.deserialize_message(&raw, &out);
  } catch (const std::exception& error) {
    std::printf("      deserialize failed: %s\n", error.what());
    return false;
  }
  return true;
}

std::string describe(const std::vector<std::int32_t>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + std::to_string(values[i]);
  }
  return out + "]";
}

}  // namespace

int main() {
  sdk::RclcppBackendOptions options;
  options.node_name = "action_test_backend";
  sdk::RclcppBackend backend(options);

  const sdk::Status init_status = backend.init();
  if (!init_status.ok()) {
    std::printf("FAIL backend init: %s\n", init_status.toString().c_str());
    return 1;
  }

  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  auto normal = std::make_shared<FibonacciServer>(
      "action_test_fibonacci_server", "action_test_fibonacci", false);
  auto refusing = std::make_shared<FibonacciServer>(
      "action_test_refusing_server", "action_test_refusing", true);
  executor->add_node(normal);
  executor->add_node(refusing);
  std::thread spinner([&executor]() {executor->spin();});

  // Discovery is asynchronous, so give the backend a moment to see the two
  // servers before asking it to use them.
  std::this_thread::sleep_for(std::chrono::seconds(2));

  const std::string action = "/action_test_fibonacci";
  const std::string type = "action_tutorials_interfaces/action/Fibonacci";

  // ---- list
  {
    std::vector<sdk::ActionInfo> actions;
    const sdk::Status status = backend.listActions(actions);
    check(status.ok(), "listActions succeeds");
    bool found = false;
    bool type_ok = false;
    for (const auto& item : actions) {
      if (item.name == action) {
        found = true;
        type_ok = item.type == type;
      }
    }
    check(found, action + " is in listActions");
    check(type_ok, action + " carries the action type, not the service type");
  }

  // ---- send + follow a goal that succeeds
  std::string goal_id;
  {
    Fibonacci::Goal goal;
    goal.order = 5;
    const sdk::Status status = backend.sendActionGoal(
        action, "", serializeGoal(goal), "cdr", 5000, goal_id);
    check(status.ok(), "sendActionGoal succeeds");
    check(!goal_id.empty(), "an accepted goal comes back with a goal id");
    check(goal_id.size() == 32, "the goal id is the 32 hex digits of the UUID");
  }

  {
    std::atomic<int> feedback_seen{0};
    std::atomic<int> terminal_seen{0};
    sdk::GoalStatus terminal_status = sdk::GoalStatus::Unknown;
    std::vector<std::int32_t> terminal_sequence;
    std::string stream_error;

    std::thread follower([&]() {
      const sdk::Status status = backend.streamActionFeedback(
          action, goal_id,
          [&](const sdk::ActionFeedback& item) {
            if (!item.terminal()) {
              Fibonacci::Feedback feedback;
              if (!deserialize(item.feedback, feedback)) {
                ++failures;
              }
              feedback_seen.fetch_add(1);
              return true;
            }
            terminal_seen.fetch_add(1);
            terminal_status = item.status;
            Fibonacci::Result result;
            if (!deserialize(item.result, result)) {
              ++failures;
            }
            terminal_sequence = result.sequence;
            return false;
          },
          []() {return false;});
      if (!status.ok()) {
        stream_error = status.toString();
      }
    });
    follower.join();

    check(stream_error.empty(), "streamActionFeedback ends cleanly (" + stream_error + ")");
    check(feedback_seen.load() > 0,
          "feedback arrived while the goal ran (" +
              std::to_string(feedback_seen.load()) + " messages)");
    check(terminal_seen.load() == 1, "exactly one terminal message closed the stream");
    check(terminal_status == sdk::GoalStatus::Succeeded, "the terminal status is Succeeded");
    check(terminal_sequence == std::vector<std::int32_t>({0, 1, 1, 2, 3, 5}),
          "the terminal message carries the result " + describe(terminal_sequence));
  }

  // ---- and the same result on its own, for callers that only poll
  {
    sdk::ActionFeedback result;
    const sdk::Status status = backend.getActionResult(action, goal_id, 5000, result);
    check(status.ok(), "getActionResult succeeds");
    check(result.status == sdk::GoalStatus::Succeeded, "getActionResult reports Succeeded");
    Fibonacci::Result decoded;
    if (deserialize(result.result, decoded)) {
      check(decoded.sequence == std::vector<std::int32_t>({0, 1, 1, 2, 3, 5}),
            "getActionResult carries the result " + describe(decoded.sequence));
    } else {
      check(false, "getActionResult result decodes");
    }
  }

  // ---- a refused goal is an answer, not a transport failure
  {
    Fibonacci::Goal goal;
    goal.order = 5;
    std::string refused_id;
    const sdk::Status status = backend.sendActionGoal(
        "/action_test_refusing", type, serializeGoal(goal), "cdr", 5000, refused_id);
    check(status.ok(), "a refused goal still reports a successful RPC");
    check(refused_id.empty(), "a refused goal comes back with an empty goal id");
  }

  // ---- cancel a goal that is still running
  {
    Fibonacci::Goal goal;
    goal.order = 40;  // two seconds at 20 Hz, so the cancel lands mid-flight
    std::string cancel_id;
    const sdk::Status sent = backend.sendActionGoal(
        action, type, serializeGoal(goal), "cdr", 5000, cancel_id);
    check(sent.ok() && !cancel_id.empty(), "a long goal is accepted");

    std::atomic<int> terminal_seen{0};
    sdk::GoalStatus terminal_status = sdk::GoalStatus::Unknown;
    std::thread follower([&]() {
      backend.streamActionFeedback(
          action, cancel_id,
          [&](const sdk::ActionFeedback& item) {
            if (item.terminal()) {
              terminal_seen.fetch_add(1);
              terminal_status = item.status;
              return false;
            }
            return true;
          },
          []() {return false;});
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(backend.cancelActionGoal(action, cancel_id), "cancelActionGoal accepts a live goal");
    follower.join();

    check(terminal_seen.load() == 1, "the canceled goal still ended the stream once");
    check(terminal_status == sdk::GoalStatus::Canceled, "the terminal status is Canceled");
    check(!backend.cancelActionGoal(action, cancel_id),
          "cancelling a goal that is already over answers false");
  }

  // ---- an unknown goal id is not a goal
  {
    sdk::ActionFeedback result;
    const sdk::Status status = backend.getActionResult(
        action, "00000000000000000000000000000000", 1000, result);
    check(status.code() == sdk::StatusCode::NotFound, "an unknown goal id is NotFound");
  }

  executor->cancel();
  spinner.join();
  backend.shutdown();

  std::printf(failures == 0 ? "all action checks passed\n" : "%d action checks FAILED\n",
              failures);
  return failures == 0 ? 0 : 1;
}
