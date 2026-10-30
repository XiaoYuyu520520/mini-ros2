// bridge_action_roundtrip - the action half of the bridge, with the network
// taken out.
//
// A goal a node on this machine sends at an action the robot serves and a goal
// the bridge forwards to the robot are the *same* operation once they reach
// shared memory: a goal lands on `<name>/_goal` with a goal id in front of it,
// the answer comes back on `<name>/_goal_ack` under that id, feedback follows on
// `<name>/_feedback`, and the result ends the goal on `<name>/_result`. The only
// thing that decides whether the two ends can talk is whether they agree on
// those six names, on the slot sizes behind them, and on the payload layouts.
//
// So this test runs the whole loop in one process, with a stand-in for the
// daemon on one side and a stand-in for a caller on the other:
//
//   1. `RemoteAction` takes the daemon's role - it creates the six channels the
//      way bridge/include/ros2_bridge/shm_topic.hpp does, advertises the name in
//      the shim graph as `remote:test`, and answers every goal with the same
//      shim -> CDR -> shim pair of conversions the daemon performs around a gRPC
//      call.
//   2. `rclcpp_action::Client` takes the caller's role - the test never names a
//      channel itself, so if the shim and the bridge ever disagreed about
//      `<name>/_goal` or about the slot size, the goal would sit in a different
//      mapping and this test would hang instead of passing.
//
// The fields are checked rather than just "something came back": a swapped pair
// of doubles would survive a byte count check, and the result code byte is the
// one thing the shim reads *before* the payload, so it is checked on its own.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ros2_bridge/action_registry.hpp"
#include "ros2_bridge/cdr_archive.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/sim_action_messages.hpp"

#include "nav2_msgs/action/back_up.hpp"
#include "nav2_msgs/action/compute_path_to_pose.hpp"
#include "nav2_msgs/action/drive_on_heading.hpp"
#include "nav2_msgs/action/follow_path.hpp"
#include "nav2_msgs/action/follow_waypoints.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav2_msgs/action/spin.hpp"
#include "nav2_msgs/action/wait.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

namespace
{

using Drive = ros2_bridge_sim::action::Drive;

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string & what)
{
  ++g_checks;
  if (ok) {
    std::printf("ok   %s\n", what.c_str());
  } else {
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
  }
}

// ------------------------------------------------------------- the stand-in

// What the remote action should do with the next goal it is handed.
struct Script
{
  bool accept = true;
  int feedback_count = 2;
  // A goal that reports progress and then keeps running: the only way it ends
  // is a cancel, which is what the cancel half of this test needs.
  bool hold_result = false;
  ros2_bridge::ShimResultCode code = ros2_bridge::ShimResultCode::Succeeded;
  // The robot's own two fields of the result payload.
  double travelled = 1.25;
  bool success = true;
  // Cancels are accepted by default, which is what a server that is still
  // driving does.
  bool cancel_accepted = true;
};

template<class ActionT>
class RemoteAction
{
public:
  using Goal = typename ActionT::Goal;
  using Feedback = typename ActionT::Feedback;
  using Result = typename ActionT::Result;

  RemoteAction(
    const std::string & name, const std::string & type, Script script,
    std::function<void(const Goal &)> expect_goal)
  : name_(name), type_(type), script_(std::move(script)), expect_goal_(std::move(expect_goal))
  {
    codec_ = ros2_bridge::find_action_codec(type);
    if (codec_ == nullptr) {
      std::printf("FAIL no codec for %s\n", type.c_str());
      ++g_failures;
      return;
    }
    std::string error;
    goals_ = ros2_bridge::create_action_channel(name, "goal", &error);
    goal_acks_ = ros2_bridge::create_action_channel(name, "goal_ack", &error);
    cancels_ = ros2_bridge::create_action_channel(name, "cancel", &error);
    cancel_acks_ = ros2_bridge::create_action_channel(name, "cancel_ack", &error);
    feedbacks_ = ros2_bridge::create_action_channel(name, "feedback", &error);
    results_ = ros2_bridge::create_action_channel(name, "result", &error);
    if (!goals_ || !goal_acks_ || !cancels_ || !cancel_acks_ || !feedbacks_ || !results_) {
      std::printf("FAIL cannot create the channels for %s: %s\n", name.c_str(), error.c_str());
      ++g_failures;
      return;
    }
    // The head of both streams the stand-in reads, exactly as the daemon does
    // when it takes over an action (`startAction`) and as `rclcpp_action::Server`
    // does in its constructor: the ring outlives its writer, so a stand-in that
    // entered at the start would replay the previous case's goal and answer it.
    goals_->seek_to_latest();
    cancels_->seek_to_latest();
    ros2_shim::graph::ProcessRegistry::instance().add_action(name, type, "remote:test");
    running_ = true;
    worker_ = std::thread([this]() {loop();});
  }

  ~RemoteAction() {stop();}

  void stop()
  {
    if (!running_.exchange(false)) {
      return;
    }
    if (worker_.joinable()) {
      worker_.join();
    }
    ros2_shim::graph::ProcessRegistry::instance().remove_action(name_, "remote:test");
  }

  int goals() const {return goals_seen_.load();}
  int cancels() const {return cancels_seen_.load();}
  int failures() const {return failures_.load();}

private:
  // What the daemon does on the way out (shim -> CDR) and then what the robot
  // would do with it (CDR -> struct, via the same archive the generated
  // messages are read with).
  void loop()
  {
    std::vector<std::uint8_t> buffer(goals_->slot_size());
    while (running_.load()) {
      std::size_t size = 0;
      if (goals_->subscribe(buffer.data(), buffer.size(), &size, 20) && size > sizeof(std::uint64_t)) {
        std::uint64_t id = 0;
        std::memcpy(&id, buffer.data(), sizeof(id));
        const ros2_bridge::Bytes shim(buffer.begin() + sizeof(id), buffer.begin() + size);
        handleGoal(id, shim);
      }

      size = 0;
      if (cancels_->subscribe(buffer.data(), buffer.size(), &size, 20) &&
        size >= sizeof(std::uint64_t))
      {
        std::uint64_t id = 0;
        std::memcpy(&id, buffer.data(), sizeof(id));
        handleCancel(id);
      }
    }
  }

  void handleGoal(std::uint64_t id, const ros2_bridge::Bytes & shim)
  {
    goals_seen_.fetch_add(1);

    ros2_bridge::Bytes cdr;
    if (!codec_->goal_to_cdr(shim, cdr)) {
      failures_.fetch_add(1);
      std::printf("FAIL %s: the goal is not a valid shim payload\n", name_.c_str());
      publishFlag(goal_acks_, id, 0);
      return;
    }

    Goal goal;
    {
      // The far end of a real bridge decodes the CDR the gRPC call delivered.
      ros2_bridge::CdrReadArchive archive(cdr);
      goal.visit(archive);
      if (!archive.ok()) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the goal does not survive CDR\n", name_.c_str());
        publishFlag(goal_acks_, id, 0);
        return;
      }
    }
    if (expect_goal_) {
      expect_goal_(goal);
    }

    publishFlag(goal_acks_, id, script_.accept ? 1 : 0);
    if (!script_.accept) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.insert(id);
    }

    for (int i = 0; i < script_.feedback_count; ++i) {
      Feedback feedback;
      feedback.travelled = 0.5 * (i + 1);
      feedback.remaining = 1.25 - feedback.travelled;
      ros2_bridge::Bytes feedback_cdr;
      ros2_grpc_sdk::cdr::Writer writer;
      {
        ros2_bridge::CdrWriteArchive archive(writer);
        feedback.visit(archive);
      }
      feedback_cdr = writer.take();
      ros2_bridge::Bytes feedback_shim;
      if (!codec_->feedback_to_shim(feedback_cdr, feedback_shim)) {
        failures_.fetch_add(1);
        std::printf("FAIL %s: the feedback does not come out of CDR\n", name_.c_str());
        continue;
      }
      publishPayload(feedbacks_, id, feedback_shim);
    }

    if (!script_.hold_result) {
      finish(id);
    }
  }

  void handleCancel(std::uint64_t id)
  {
    cancels_seen_.fetch_add(1);
    bool live = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live = live_.count(id) != 0;
    }
    const bool accepted = live && script_.cancel_accepted;
    publishFlag(cancel_acks_, id, accepted ? 1 : 0);
    if (accepted) {
      finish(id, ros2_bridge::ShimResultCode::Canceled);
    }
  }

  void finish(
    std::uint64_t id,
    ros2_bridge::ShimResultCode code = ros2_bridge::ShimResultCode::Succeeded)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.erase(id);
    }
    if (code == ros2_bridge::ShimResultCode::Succeeded) {
      code = script_.code;
    }

    Result result;
    result.travelled = script_.travelled;
    result.success = script_.success;
    ros2_bridge::Bytes result_cdr;
    ros2_grpc_sdk::cdr::Writer writer;
    {
      ros2_bridge::CdrWriteArchive archive(writer);
      result.visit(archive);
    }
    result_cdr = writer.take();
    ros2_bridge::Bytes result_shim;
    if (!codec_->result_to_shim(result_cdr, result_shim)) {
      failures_.fetch_add(1);
      std::printf("FAIL %s: the result does not come out of CDR\n", name_.c_str());
      return;
    }

    // The result code is the byte in front of the payload - exactly what the
    // daemon writes and the shim's client reads back.
    ros2_bridge::Bytes out(sizeof(id) + 1 + result_shim.size());
    std::memcpy(out.data(), &id, sizeof(id));
    out[sizeof(id)] = static_cast<std::uint8_t>(code);
    std::memcpy(out.data() + sizeof(id) + 1, result_shim.data(), result_shim.size());
    publish(results_, out);
  }

  void publishFlag(
    const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id, std::uint8_t value)
  {
    ros2_bridge::Bytes out(sizeof(id) + 1, 0);
    std::memcpy(out.data(), &id, sizeof(id));
    out[sizeof(id)] = value;
    publish(channel, out);
  }

  void publishPayload(
    const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id,
    const ros2_bridge::Bytes & payload)
  {
    ros2_bridge::Bytes out(sizeof(id) + payload.size());
    std::memcpy(out.data(), &id, sizeof(id));
    std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
    publish(channel, out);
  }

  void publish(
    const std::shared_ptr<shm_transport::ShmChannel> & channel, const ros2_bridge::Bytes & payload)
  {
    if (!channel->publish(payload.data(), payload.size())) {
      failures_.fetch_add(1);
      std::printf("FAIL %s: a %zu byte message does not fit its slot\n", name_.c_str(),
        payload.size());
    }
  }

  std::string name_;
  std::string type_;
  Script script_;
  std::function<void(const Goal &)> expect_goal_;
  const ros2_bridge::ActionCodec * codec_ = nullptr;
  std::shared_ptr<shm_transport::ShmChannel> goals_;
  std::shared_ptr<shm_transport::ShmChannel> goal_acks_;
  std::shared_ptr<shm_transport::ShmChannel> cancels_;
  std::shared_ptr<shm_transport::ShmChannel> cancel_acks_;
  std::shared_ptr<shm_transport::ShmChannel> feedbacks_;
  std::shared_ptr<shm_transport::ShmChannel> results_;
  std::mutex mutex_;
  std::set<std::uint64_t> live_;
  std::atomic<bool> running_{false};
  std::atomic<int> goals_seen_{0};
  std::atomic<int> cancels_seen_{0};
  std::atomic<int> failures_{0};
  std::thread worker_;
};

// --------------------------------------------------------------- the caller

using ClientT = rclcpp_action::Client<Drive>;
using GoalHandle = ClientT::GoalHandleSharedPtr;
using WrappedResult = ClientT::WrappedResult;

// Waits for a future with the shim node's own spinning, the way nav2's
// behavior tree does.
template<class FutureT>
bool waitFor(rclcpp::Node & node, FutureT & future, double seconds)
{
  const auto deadline =
    std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000));
  while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    node.spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

// `build_goal` and then back into the shim layout: the two steps
// `ros2 action send_goal <name> <type> field=value ...` takes before it writes
// anything onto the goal channel.
template<class Goal>
bool buildGoal(
  const ros2_bridge::ActionCodec & codec,
  const std::vector<ros2_bridge::FieldAssign> & fields, Goal & out, std::string & error)
{
  ros2_bridge::Bytes shim;
  if (!codec.build_goal(fields, shim, error)) {
    return false;
  }
  return ros2_shim::deserialize(shim, out);
}

// A message built here, through CDR, back into the shim layout - the direction
// the daemon runs on feedback and results arriving from the robot.
template<class Message>
bool transcodeBack(
  const std::function<bool(const ros2_bridge::Bytes &, ros2_bridge::Bytes &)> & convert,
  const Message & message, Message & out)
{
  ros2_grpc_sdk::cdr::Writer writer;
  {
    ros2_bridge::CdrWriteArchive archive(writer);
    message.visit(archive);
  }
  ros2_bridge::Bytes shim;
  if (!convert(writer.take(), shim)) {
    return false;
  }
  return ros2_shim::deserialize(shim, out);
}

}  // namespace

int main()
{
  // The checks below are the only output this test produces, and a hang is the
  // failure mode worth diagnosing most, so the lines go out as they happen
  // rather than when a 4 KiB buffer fills.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // The four numbers the bridge writes into a result are the shim's own result
  // codes, so the two cannot quietly drift apart.
  check(
    static_cast<std::uint8_t>(ros2_bridge::ShimResultCode::Unknown) ==
    static_cast<std::uint8_t>(rclcpp_action::ResultCode::UNKNOWN),
    "ShimResultCode::Unknown is rclcpp_action::ResultCode::UNKNOWN");
  check(
    static_cast<std::uint8_t>(ros2_bridge::ShimResultCode::Succeeded) ==
    static_cast<std::uint8_t>(rclcpp_action::ResultCode::SUCCEEDED),
    "ShimResultCode::Succeeded is rclcpp_action::ResultCode::SUCCEEDED");
  check(
    static_cast<std::uint8_t>(ros2_bridge::ShimResultCode::Aborted) ==
    static_cast<std::uint8_t>(rclcpp_action::ResultCode::ABORTED),
    "ShimResultCode::Aborted is rclcpp_action::ResultCode::ABORTED");
  check(
    static_cast<std::uint8_t>(ros2_bridge::ShimResultCode::Canceled) ==
    static_cast<std::uint8_t>(rclcpp_action::ResultCode::CANCELED),
    "ShimResultCode::Canceled is rclcpp_action::ResultCode::CANCELED");

  // The registry itself.
  const ros2_bridge::ActionCodec * codec =
    ros2_bridge::find_action_codec("ros2_bridge_sim/action/Drive");
  check(codec != nullptr, "the Drive action has a codec");
  check(
    ros2_bridge::find_action_codec("ros2_bridge_sim/Drive") == codec,
    "ros2_bridge_sim/Drive resolves to the same codec");
  check(
    ros2_bridge::canonical_action_type("ros2_bridge_sim/Drive") == "ros2_bridge_sim/action/Drive",
    "canonical_action_type adds the kind");
  check(
    ros2_bridge::action_goal_type("ros2_bridge_sim/action/Drive") ==
    "ros2_bridge_sim/action/Drive_Goal",
    "the goal type of Drive is named the ROS2 way");
  check(
    ros2_bridge::action_feedback_type("ros2_bridge_sim/action/Drive") ==
    "ros2_bridge_sim/action/Drive_Feedback",
    "the feedback type of Drive is named the ROS2 way");
  check(
    ros2_bridge::action_result_type("ros2_bridge_sim/action/Drive") ==
    "ros2_bridge_sim/action/Drive_Result",
    "the result type of Drive is named the ROS2 way");
  check(
    ros2_bridge::shim_result_code(ros2_grpc_sdk::GoalStatus::Succeeded) ==
    ros2_bridge::ShimResultCode::Succeeded,
    "a succeeded goal becomes SUCCEEDED");
  check(
    ros2_bridge::shim_result_code(ros2_grpc_sdk::GoalStatus::Canceled) ==
    ros2_bridge::ShimResultCode::Canceled,
    "a canceled goal becomes CANCELED");
  check(
    ros2_bridge::shim_result_code(ros2_grpc_sdk::GoalStatus::Aborted) ==
    ros2_bridge::ShimResultCode::Aborted,
    "an aborted goal becomes ABORTED");
  check(
    ros2_bridge::shim_result_code(ros2_grpc_sdk::GoalStatus::Rejected) ==
    ros2_bridge::ShimResultCode::Aborted,
    "a rejected goal is a failure, not a success");

  // A goal's fields, and the feedback and result, all the way through CDR and
  // back - the conversion the daemon runs in each direction.
  {
    const auto * drive_codec = ros2_bridge::find_action_codec("ros2_bridge_sim/action/Drive");
    Drive::Goal goal;
    goal.distance = -2.5;
    ros2_bridge::Bytes goal_cdr;
    check(drive_codec->goal_to_cdr(ros2_shim::serialize(goal), goal_cdr), "a goal encodes to CDR");
    ros2_bridge::CdrReadArchive reader(goal_cdr);
    Drive::Goal decoded;
    decoded.visit(reader);
    check(reader.ok() && decoded.distance == -2.5, "the goal's distance survives the round trip");

    Drive::Feedback feedback;
    feedback.travelled = 0.75;
    feedback.remaining = 1.75;
    ros2_grpc_sdk::cdr::Writer writer;
    {
      ros2_bridge::CdrWriteArchive archive(writer);
      feedback.visit(archive);
    }
    ros2_bridge::Bytes feedback_shim;
    check(
      drive_codec->feedback_to_shim(writer.take(), feedback_shim),
      "feedback decodes out of CDR");
    Drive::Feedback feedback_back;
    check(
      ros2_shim::deserialize(feedback_shim, feedback_back) &&
      feedback_back.travelled == 0.75 && feedback_back.remaining == 1.75,
      "the feedback's two fields survive the round trip");

    Drive::Result result;
    result.travelled = 1.125;
    result.success = true;
    ros2_grpc_sdk::cdr::Writer result_writer;
    {
      ros2_bridge::CdrWriteArchive archive(result_writer);
      result.visit(archive);
    }
    ros2_bridge::Bytes result_shim;
    check(drive_codec->result_to_shim(result_writer.take(), result_shim), "a result decodes out of CDR");
    Drive::Result result_back;
    check(
      ros2_shim::deserialize(result_shim, result_back) &&
      result_back.travelled == 1.125 && result_back.success,
      "the result's fields survive the round trip");

    // The goal builder, i.e. what `ros2 action send_goal /sim/drive ... distance=1`
    // does before it publishes anything.
    ros2_bridge::Bytes built;
    std::string error;
    check(
      drive_codec->build_goal({{"distance", "1.25"}}, built, error), "a goal can be built by hand");
    Drive::Goal built_goal;
    check(
      ros2_shim::deserialize(built, built_goal) && built_goal.distance == 1.25,
      "the built goal carries the value that was asked for");
    check(
      !drive_codec->build_goal({{"distance", "not a number"}}, built, error),
      "a goal with an unparsable value is refused");
    check(
      !drive_codec->build_goal({{"distance_travelled", "1"}}, built, error),
      "a goal with an unknown field is refused");

    // What the daemon publishes when a robot ends a goal without a result
    // payload - a cancel before the goal produced anything. It has to decode:
    // the caller reads this payload, and an empty one would leave it waiting
    // for a message that already arrived.
    check(!drive_codec->default_result.empty(), "the codec carries a default result");
    Drive::Result defaulted;
    check(
      ros2_shim::deserialize(drive_codec->default_result, defaulted) &&
      defaulted.travelled == 0.0 && !defaulted.success,
      "the default result is a default-constructed Drive_Result");
  }

  // -------------------------------------------------------------------------
  // The nav2 action set.
  //
  // The codec table is what makes an action served *on the robot* look like an
  // action served on this machine, so every entry has to be wired to the right
  // generated structs, in both directions, and every goal it claims to be able
  // to build has to build. What the robot then does with it is the other side's
  // business - and its own test (ros2_bridge_sim/tests/test_services.py).
  // -------------------------------------------------------------------------
  {
    struct Entry
    {
      const char * type;
      const char * goal;
      const char * feedback;
      const char * result;
    };
    // Every action nav2_msgs declares, i.e. every `.action` file upstream
    // ships. The table is what `ros2 action list -t` reads, so a type missing
    // here is a type the robot serves and this machine cannot call.
    const Entry entries[] = {
      {"nav2_msgs/action/Spin", "nav2_msgs/action/Spin_Goal",
        "nav2_msgs/action/Spin_Feedback", "nav2_msgs/action/Spin_Result"},
      {"nav2_msgs/action/BackUp", "nav2_msgs/action/BackUp_Goal",
        "nav2_msgs/action/BackUp_Feedback", "nav2_msgs/action/BackUp_Result"},
      {"nav2_msgs/action/DriveOnHeading", "nav2_msgs/action/DriveOnHeading_Goal",
        "nav2_msgs/action/DriveOnHeading_Feedback", "nav2_msgs/action/DriveOnHeading_Result"},
      {"nav2_msgs/action/Wait", "nav2_msgs/action/Wait_Goal",
        "nav2_msgs/action/Wait_Feedback", "nav2_msgs/action/Wait_Result"},
      {"nav2_msgs/action/AssistedTeleop", "nav2_msgs/action/AssistedTeleop_Goal",
        "nav2_msgs/action/AssistedTeleop_Feedback", "nav2_msgs/action/AssistedTeleop_Result"},
      {"nav2_msgs/action/DummyBehavior", "nav2_msgs/action/DummyBehavior_Goal",
        "nav2_msgs/action/DummyBehavior_Feedback", "nav2_msgs/action/DummyBehavior_Result"},
      {"nav2_msgs/action/NavigateToPose", "nav2_msgs/action/NavigateToPose_Goal",
        "nav2_msgs/action/NavigateToPose_Feedback", "nav2_msgs/action/NavigateToPose_Result"},
      {"nav2_msgs/action/NavigateThroughPoses", "nav2_msgs/action/NavigateThroughPoses_Goal",
        "nav2_msgs/action/NavigateThroughPoses_Feedback",
        "nav2_msgs/action/NavigateThroughPoses_Result"},
      {"nav2_msgs/action/ComputePathToPose", "nav2_msgs/action/ComputePathToPose_Goal",
        "nav2_msgs/action/ComputePathToPose_Feedback",
        "nav2_msgs/action/ComputePathToPose_Result"},
      {"nav2_msgs/action/ComputePathThroughPoses",
        "nav2_msgs/action/ComputePathThroughPoses_Goal",
        "nav2_msgs/action/ComputePathThroughPoses_Feedback",
        "nav2_msgs/action/ComputePathThroughPoses_Result"},
      {"nav2_msgs/action/ComputeRoute", "nav2_msgs/action/ComputeRoute_Goal",
        "nav2_msgs/action/ComputeRoute_Feedback", "nav2_msgs/action/ComputeRoute_Result"},
      {"nav2_msgs/action/ComputeAndTrackRoute",
        "nav2_msgs/action/ComputeAndTrackRoute_Goal",
        "nav2_msgs/action/ComputeAndTrackRoute_Feedback",
        "nav2_msgs/action/ComputeAndTrackRoute_Result"},
      {"nav2_msgs/action/FollowPath", "nav2_msgs/action/FollowPath_Goal",
        "nav2_msgs/action/FollowPath_Feedback", "nav2_msgs/action/FollowPath_Result"},
      {"nav2_msgs/action/FollowWaypoints", "nav2_msgs/action/FollowWaypoints_Goal",
        "nav2_msgs/action/FollowWaypoints_Feedback",
        "nav2_msgs/action/FollowWaypoints_Result"},
      {"nav2_msgs/action/FollowGPSWaypoints", "nav2_msgs/action/FollowGPSWaypoints_Goal",
        "nav2_msgs/action/FollowGPSWaypoints_Feedback",
        "nav2_msgs/action/FollowGPSWaypoints_Result"},
      {"nav2_msgs/action/FollowObject", "nav2_msgs/action/FollowObject_Goal",
        "nav2_msgs/action/FollowObject_Feedback", "nav2_msgs/action/FollowObject_Result"},
      {"nav2_msgs/action/SmoothPath", "nav2_msgs/action/SmoothPath_Goal",
        "nav2_msgs/action/SmoothPath_Feedback", "nav2_msgs/action/SmoothPath_Result"},
      {"nav2_msgs/action/DockRobot", "nav2_msgs/action/DockRobot_Goal",
        "nav2_msgs/action/DockRobot_Feedback", "nav2_msgs/action/DockRobot_Result"},
      {"nav2_msgs/action/UndockRobot", "nav2_msgs/action/UndockRobot_Goal",
        "nav2_msgs/action/UndockRobot_Feedback", "nav2_msgs/action/UndockRobot_Result"},
    };
    for (const auto & entry : entries) {
      const std::string type = entry.type;
      const auto * codec = ros2_bridge::find_action_codec(type);
      check(codec != nullptr, "a codec is registered for " + type);
      if (codec == nullptr) {
        continue;
      }
      check(codec->goal_type == entry.goal, type + " names its goal the ROS2 way");
      check(codec->feedback_type == entry.feedback, type + " names its feedback the ROS2 way");
      check(codec->result_type == entry.result, type + " names its result the ROS2 way");
      check(!codec->default_result.empty(), type + " carries a default result");

      // Every row has to be able to build a goal out of nothing and to refuse
      // a field its goal does not have; the second one is what proves the row
      // walks the right goal message rather than dropping what it cannot place.
      ros2_bridge::Bytes payload;
      std::string error;
      check(
        codec->build_goal({}, payload, error) && !payload.empty(),
        type + " builds a default goal: " + error);
      check(
        !codec->build_goal({{"definitely_not_a_field", "1"}}, payload, error),
        type + " refuses a field its goal does not have");

      // ... and every row has to be able to say what it holds: a row with no
      // printer hands a caller a byte count where it asked for an answer.
      const std::string printed_goal = codec->describe_goal(payload);
      check(
        printed_goal.find("no pretty printer") == std::string::npos &&
        printed_goal.find("undecodable") == std::string::npos,
        type + " can print its own goal");
      const std::string printed_result = codec->describe_result(codec->default_result);
      check(
        printed_result.find("no pretty printer") == std::string::npos &&
        printed_result.find("undecodable") == std::string::npos,
        type + " can print its own result");

      // The spelling a caller is most likely to produce: the message package
      // and the name, with the `action/` kind left out. `ros2 action list -t`
      // prints the canonical one, so the loose one only ever comes off a
      // command line - which is exactly where it has to work.
      const std::string loose =
        "nav2_msgs/" + type.substr(std::string("nav2_msgs/action/").size());
      check(
        ros2_bridge::find_action_codec(loose) == codec,
        loose + " resolves to the same codec as " + type);
    }

    // The count is derived from the table rather than written out, so a row
    // added above cannot leave this check behind - and the one thing the shim
    // graph is asked is whether the sim's own action is still the only row
    // that is not nav2's.
    check(
      ros2_bridge::supported_actions().size() == 1 + sizeof(entries) / sizeof(entries[0]),
      "the registry holds the sim's own action and every nav2 one");

    // What the command line can say, one row per field of the goals that did
    // not have a section of their own below: `ros2 action send_goal` builds the
    // goal through `build_goal` and prints it through `describe_goal`, so a row
    // here is a promise both halves keep.
    struct Buildable
    {
      const char * type;
      const char * field;
      const char * value;
      const char * needle;
    };
    const Buildable buildable[] = {
      {"nav2_msgs/action/AssistedTeleop", "time_allowance.sec", "5",
        "time_allowance: 5s+0ns"},
      {"nav2_msgs/action/SmoothPath", "smoother_id", "SavitzkyGolay",
        "smoother_id: \"SavitzkyGolay\""},
      {"nav2_msgs/action/SmoothPath", "check_for_collisions", "true",
        "check_for_collisions: true"},
      {"nav2_msgs/action/SmoothPath", "path.header.frame_id", "map",
        "path: 0 pose(s) in \"map\""},
      {"nav2_msgs/action/ComputeRoute", "start_id", "3", "start_id: 3"},
      {"nav2_msgs/action/ComputeRoute", "goal_id", "7", "goal_id: 7"},
      {"nav2_msgs/action/ComputeRoute", "use_poses", "true", "use_poses: true"},
      {"nav2_msgs/action/ComputeRoute", "start.pose.position.x", "-1.5",
        "start: (-1.5, 0, 0)"},
      {"nav2_msgs/action/ComputeRoute", "goal.header.frame_id", "map",
        "goal: (0, 0, 0) in \"map\""},
      {"nav2_msgs/action/ComputeAndTrackRoute", "start_id", "11", "start_id: 11"},
      {"nav2_msgs/action/ComputeAndTrackRoute", "goal.pose.orientation.w", "1",
        "goal: (0, 0, 0) in \"\" yaw-ish quaternion (0, 0, 0, 1)"},
      {"nav2_msgs/action/ComputePathThroughPoses", "planner_id", "GridBased",
        "planner_id: \"GridBased\""},
      {"nav2_msgs/action/ComputePathThroughPoses", "start.pose.position.y", "-2.25",
        "start: (0, -2.25, 0)"},
      {"nav2_msgs/action/ComputePathThroughPoses", "use_start", "true",
        "use_start: true"},
      {"nav2_msgs/action/NavigateThroughPoses", "behavior_tree", "custom.xml",
        "behavior_tree: \"custom.xml\""},
      {"nav2_msgs/action/DockRobot", "dock_id", "dock-1", "dock_id: \"dock-1\""},
      {"nav2_msgs/action/DockRobot", "use_dock_id", "false", "use_dock_id: false"},
      {"nav2_msgs/action/DockRobot", "max_staging_time", "500.0",
        "max_staging_time: 500"},
      {"nav2_msgs/action/DockRobot", "navigate_to_staging_pose", "false",
        "navigate_to_staging_pose: false"},
      {"nav2_msgs/action/DockRobot", "dock_pose.pose.orientation.w", "1.0",
        "dock_pose: (0, 0, 0) in \"\" yaw-ish quaternion (0, 0, 0, 1)"},
      {"nav2_msgs/action/UndockRobot", "max_undocking_time", "15.5",
        "max_undocking_time: 15.5"},
      {"nav2_msgs/action/UndockRobot", "dock_type", "dock_type_a",
        "dock_type: \"dock_type_a\""},
      {"nav2_msgs/action/FollowObject", "pose_topic", "/detections",
        "pose_topic: \"/detections\""},
      {"nav2_msgs/action/FollowObject", "tracked_frame", "odom",
        "tracked_frame: \"odom\""},
      {"nav2_msgs/action/FollowObject", "max_duration.sec", "30",
        "max_duration: 30s+0ns"},
      {"nav2_msgs/action/FollowGPSWaypoints", "number_of_loops", "2",
        "number_of_loops: 2"},
      {"nav2_msgs/action/FollowGPSWaypoints", "goal_index", "1", "goal_index: 1"},
      {"nav2_msgs/action/DummyBehavior", "command.data", "hello",
        "command: \"hello\""},
    };
    for (const auto & row : buildable) {
      const auto * codec = ros2_bridge::find_action_codec(row.type);
      const std::string what =
        std::string(row.type) + " " + row.field + "=" + row.value;
      ros2_bridge::Bytes payload;
      std::string error;
      const bool built = codec != nullptr && codec->build_goal(
        {{row.field, row.value}}, payload, error);
      check(built, what + " can be built from the command line: " + error);
      if (built) {
        const std::string printed = codec->describe_goal(payload);
        check(
          printed.find(row.needle) != std::string::npos,
          what + " prints as \"" + row.needle + "\"");
      }
      check(
        codec != nullptr &&
        !codec->build_goal({{"definitely_not_a_field", "1"}}, payload, error),
        std::string(row.type) + " refuses a field it does not have");
    }

    check(
      ros2_bridge::find_action_codec("nav2_msgs/action/NavigateToPose") != nullptr &&
      ros2_bridge::find_action_codec("example_interfaces/action/Fibonacci") == nullptr,
      "an action the robot does not serve has no codec");
  }

  // Spin: a goal with a nested `builtin_interfaces/Duration`, both ways.
  {
    const auto * codec = ros2_bridge::find_action_codec("nav2_msgs/action/Spin");
    std::string error;
    nav2_msgs::action::Spin::Goal goal;
    check(
      buildGoal(*codec,
        {{"target_yaw", "1.5"}, {"time_allowance.sec", "3"},
          {"time_allowance.nanosec", "500000000"}, {"disable_collision_checks", "true"}},
        goal, error),
      "a Spin goal can be built from the command line: " + error);
    check(
      goal.target_yaw == 1.5f && goal.time_allowance.sec == 3 &&
      goal.time_allowance.nanosec == 500000000u && goal.disable_collision_checks,
      "every field of the Spin goal landed where it belongs");
    check(
      !buildGoal(*codec, {{"target_yaw", "spinning"}}, goal, error),
      "an unparsable Spin goal is refused");
    check(
      !buildGoal(*codec, {{"target", "1.5"}}, goal, error),
      "a field Spin does not have is refused rather than ignored");

    // shim -> CDR -> shim, i.e. what the robot's action server receives.
    ros2_bridge::Bytes cdr;
    check(codec->goal_to_cdr(ros2_shim::serialize(goal), cdr), "the Spin goal encodes to CDR");
    ros2_bridge::CdrReadArchive reader(cdr);
    nav2_msgs::action::Spin::Goal decoded;
    decoded.visit(reader);
    check(
      reader.ok() && decoded.target_yaw == 1.5f && decoded.time_allowance.sec == 3 &&
      decoded.time_allowance.nanosec == 500000000u && decoded.disable_collision_checks,
      "the Spin goal survives CDR");

    nav2_msgs::action::Spin::Feedback feedback;
    feedback.angular_distance_traveled = 0.75f;
    nav2_msgs::action::Spin::Feedback feedback_back;
    check(
      transcodeBack(codec->feedback_to_shim, feedback, feedback_back) &&
      feedback_back.angular_distance_traveled == 0.75f,
      "Spin feedback decodes out of CDR");
    nav2_msgs::action::Spin::Result result;
    result.total_elapsed_time.sec = 2;
    result.total_elapsed_time.nanosec = 250000000u;
    result.error_code = 0;
    nav2_msgs::action::Spin::Result result_back;
    check(
      transcodeBack(codec->result_to_shim, result, result_back) &&
      result_back.total_elapsed_time.sec == 2 &&
      result_back.total_elapsed_time.nanosec == 250000000u &&
      result_back.error_code == 0,
      "a Spin result decodes out of CDR");

    // The narrow-field printing trap: a `uint16_t` error code is a character to
    // `operator<<`, so the one line a caller reads after a failed behavior used
    // to come out with a NUL in it.
    const std::string described = codec->describe_result(ros2_shim::serialize(result_back));
    check(
      described.find("error_code: 0") != std::string::npos &&
      described.find('\0') == std::string::npos,
      "the result printer widens the error code instead of printing a NUL");
  }

  // BackUp / DriveOnHeading, which upstream gives the same goal message.
  for (const char * type : {"nav2_msgs/action/BackUp", "nav2_msgs/action/DriveOnHeading"}) {
    const auto * codec = ros2_bridge::find_action_codec(type);
    std::string error;
    if (std::string(type) == "nav2_msgs/action/BackUp") {
      nav2_msgs::action::BackUp::Goal goal;
      check(
        buildGoal(*codec,
          {{"target.x", "-0.4"}, {"speed", "-0.2"}, {"time_allowance.sec", "5"},
            {"disable_collision_checks", "false"}},
          goal, error),
        "a BackUp goal can be built from the command line: " + error);
      check(
        goal.target.x == -0.4 && goal.target.y == 0.0 && goal.target.z == 0.0 &&
        goal.speed == -0.2f && goal.time_allowance.sec == 5 && !goal.disable_collision_checks,
        "the BackUp goal's fields land where they should");
      check(
        !buildGoal(*codec, {{"target", "(1, 2)"}}, goal, error),
        "the point in a BackUp goal cannot be set as one field");
      nav2_msgs::action::BackUp::Feedback feedback;
      feedback.distance_traveled = 0.31f;
      nav2_msgs::action::BackUp::Feedback feedback_back;
      check(
        transcodeBack(codec->feedback_to_shim, feedback, feedback_back) &&
        feedback_back.distance_traveled == 0.31f,
        "BackUp feedback decodes out of CDR");
    } else {
      nav2_msgs::action::DriveOnHeading::Goal goal;
      check(
        buildGoal(*codec, {{"target.x", "0.5"}, {"speed", "0.25"}}, goal, error),
        "a DriveOnHeading goal can be built from the command line: " + error);
      check(
        goal.target.x == 0.5 && goal.speed == 0.25f,
        "the DriveOnHeading goal's fields land where they should");
    }
  }

  // Wait: the whole goal is one duration.
  {
    const auto * codec = ros2_bridge::find_action_codec("nav2_msgs/action/Wait");
    std::string error;
    nav2_msgs::action::Wait::Goal goal;
    check(
      buildGoal(*codec, {{"time.sec", "1"}, {"time.nanosec", "500000000"}}, goal, error),
      "a Wait goal can be built from the command line: " + error);
    check(
      goal.time.sec == 1 && goal.time.nanosec == 500000000u,
      "the Wait goal carries the duration it was given");
    nav2_msgs::action::Wait::Feedback feedback;
    feedback.time_left.sec = 0;
    feedback.time_left.nanosec = 250000000u;
    nav2_msgs::action::Wait::Feedback feedback_back;
    check(
      transcodeBack(codec->feedback_to_shim, feedback, feedback_back) &&
      feedback_back.time_left.nanosec == 250000000u,
      "Wait feedback decodes out of CDR");
  }

  // NavigateToPose: the goal the whole project exists to deliver, with a
  // `PoseStamped` nested two levels deep.
  {
    const auto * codec = ros2_bridge::find_action_codec("nav2_msgs/action/NavigateToPose");
    std::string error;
    nav2_msgs::action::NavigateToPose::Goal goal;
    check(
      buildGoal(*codec,
        {{"pose.header.frame_id", "map"}, {"pose.header.stamp.sec", "7"},
          {"pose.pose.position.x", "1.5"}, {"pose.pose.position.y", "-0.5"},
          {"pose.pose.orientation.w", "1"}, {"behavior_tree", "custom.xml"}},
        goal, error),
      "a NavigateToPose goal can be built from the command line: " + error);
    check(
      goal.pose.header.frame_id == "map" && goal.pose.header.stamp.sec == 7 &&
      goal.pose.pose.position.x == 1.5 && goal.pose.pose.position.y == -0.5 &&
      goal.pose.pose.orientation.w == 1.0 && goal.behavior_tree == "custom.xml",
      "the NavigateToPose goal's fields land where they should");
    check(
      !buildGoal(*codec, {{"pose.pose.position.q", "1"}}, goal, error),
      "a field that is not a field of PoseStamped is refused");

    ros2_bridge::Bytes cdr;
    check(codec->goal_to_cdr(ros2_shim::serialize(goal), cdr), "the goal encodes to CDR");
    ros2_bridge::CdrReadArchive reader(cdr);
    nav2_msgs::action::NavigateToPose::Goal decoded;
    decoded.visit(reader);
    check(
      reader.ok() && decoded.pose.pose.position.x == 1.5 &&
      decoded.behavior_tree == "custom.xml",
      "the nested pose survives CDR");

    nav2_msgs::action::NavigateToPose::Feedback feedback;
    feedback.current_pose.header.frame_id = "map";
    feedback.current_pose.pose.position.x = 0.75;
    feedback.navigation_time.sec = 4;
    feedback.number_of_recoveries = -1;
    feedback.distance_remaining = 0.6f;
    nav2_msgs::action::NavigateToPose::Feedback feedback_back;
    check(
      transcodeBack(codec->feedback_to_shim, feedback, feedback_back) &&
      feedback_back.current_pose.pose.position.x == 0.75 &&
      feedback_back.navigation_time.sec == 4 &&
      feedback_back.number_of_recoveries == -1 &&
      feedback_back.distance_remaining == 0.6f,
      "NavigateToPose feedback decodes out of CDR");
    check(
      codec->describe_feedback(ros2_shim::serialize(feedback_back)).find(
        "number_of_recoveries: -1") != std::string::npos,
      "a signed narrow field prints as a number, not as a character");
  }

  // ComputePathToPose / FollowPath / FollowWaypoints: the navigator's own
  // actions, whose payloads carry sequences of messages.
  {
    const auto * compute = ros2_bridge::find_action_codec("nav2_msgs/action/ComputePathToPose");
    std::string error;
    nav2_msgs::action::ComputePathToPose::Goal goal;
    check(
      buildGoal(*compute,
        {{"goal.pose.position.x", "3"}, {"start.pose.position.x", "0"},
          {"planner_id", "GridBased"}, {"use_start", "true"}},
        goal, error),
      "a ComputePathToPose goal can be built from the command line: " + error);
    check(
      goal.goal.pose.position.x == 3.0 && goal.start.pose.position.x == 0.0 &&
      goal.planner_id == "GridBased" && goal.use_start,
      "the ComputePathToPose goal's fields land where they should");
    // The sequence a goal like this really carries has no scalar spelling, so
    // it has to be said out loud rather than quietly ignored.
    check(
      !buildGoal(*compute, {{"viapoints", "0,0"}}, goal, error),
      "viapoints cannot be set from the command line, and says so");

    nav2_msgs::action::ComputePathToPose::Result result;
    result.path.poses.resize(3);
    result.path.poses[2].pose.position.x = 3.0;
    result.planning_time.sec = 0;
    result.planning_time.nanosec = 4000000u;
    nav2_msgs::action::ComputePathToPose::Result result_back;
    check(
      transcodeBack(compute->result_to_shim, result, result_back) &&
      result_back.path.poses.size() == 3 &&
      result_back.path.poses[2].pose.position.x == 3.0 &&
      result_back.planning_time.nanosec == 4000000u,
      "a path with poses in it decodes out of CDR");

    // A feedback message with no fields at all is legal, and it still has to
    // come back as a message rather than as a decode failure.
    nav2_msgs::action::ComputePathToPose::Feedback empty_feedback;
    nav2_msgs::action::ComputePathToPose::Feedback empty_feedback_back;
    check(
      transcodeBack(compute->feedback_to_shim, empty_feedback, empty_feedback_back),
      "a feedback message with no fields transcodes");

    const auto * follow = ros2_bridge::find_action_codec("nav2_msgs/action/FollowPath");
    nav2_msgs::action::FollowPath::Goal follow_goal;
    check(
      buildGoal(*follow,
        {{"path.header.frame_id", "odom"}, {"controller_id", "FollowPath"},
          {"goal_checker_id", "goal_checker"}, {"progress_checker_id", "progress_checker"},
          {"path_handler_id", "path_handler"}},
        follow_goal, error),
      "a FollowPath goal can be built from the command line: " + error);
    check(
      follow_goal.path.header.frame_id == "odom" &&
      follow_goal.controller_id == "FollowPath" &&
      follow_goal.path_handler_id == "path_handler",
      "the FollowPath goal's fields land where they should");
    nav2_msgs::action::FollowPath::Feedback follow_feedback;
    follow_feedback.tracking_feedback.header.frame_id = "odom";
    follow_feedback.tracking_feedback.distance_to_goal = 1.25f;
    follow_feedback.tracking_feedback.current_path_index = 4;
    nav2_msgs::action::FollowPath::Feedback follow_feedback_back;
    check(
      transcodeBack(follow->feedback_to_shim, follow_feedback, follow_feedback_back) &&
      follow_feedback_back.tracking_feedback.distance_to_goal == 1.25f &&
      follow_feedback_back.tracking_feedback.current_path_index == 4,
      "the nested tracking feedback survives CDR");

    const auto * waypoints =
      ros2_bridge::find_action_codec("nav2_msgs/action/FollowWaypoints");
    nav2_msgs::action::FollowWaypoints::Goal waypoints_goal;
    check(
      buildGoal(*waypoints, {{"number_of_loops", "2"}, {"goal_index", "1"}},
        waypoints_goal, error),
      "a FollowWaypoints goal can be built from the command line: " + error);
    check(
      waypoints_goal.number_of_loops == 2u && waypoints_goal.goal_index == 1u,
      "the FollowWaypoints goal's fields land where they should");
    nav2_msgs::action::FollowWaypoints::Feedback waypoints_feedback;
    waypoints_feedback.current_waypoint = 1u;
    nav2_msgs::action::FollowWaypoints::Feedback waypoints_feedback_back;
    check(
      transcodeBack(waypoints->feedback_to_shim, waypoints_feedback, waypoints_feedback_back) &&
      waypoints_feedback_back.current_waypoint == 1u,
      "FollowWaypoints feedback decodes out of CDR");
    nav2_msgs::action::FollowWaypoints::Result waypoints_result;
    waypoints_result.missed_waypoints.resize(1);
    waypoints_result.missed_waypoints[0].waypoint_index = 2u;
    waypoints_result.missed_waypoints[0].error_msg = "timed out";
    waypoints_result.error_code = 0;
    nav2_msgs::action::FollowWaypoints::Result waypoints_result_back;
    check(
      transcodeBack(waypoints->result_to_shim, waypoints_result, waypoints_result_back) &&
      waypoints_result_back.missed_waypoints.size() == 1 &&
      waypoints_result_back.missed_waypoints[0].waypoint_index == 2u &&
      waypoints_result_back.missed_waypoints[0].error_msg == "timed out",
      "a missed waypoint decodes out of CDR");
  }

  // -------------------------------------------------------------------------
  // One goal, end to end: the client sends it, the stand-in daemon answers.
  // -------------------------------------------------------------------------
  const std::string name = "/test/drive";
  double seen_distance = 0.0;
  int seen_goals = 0;

  Script script;
  auto remote = std::make_unique<RemoteAction<Drive>>(
    name, "ros2_bridge_sim/action/Drive", script,
    [&](const Drive::Goal & goal) {
      ++seen_goals;
      seen_distance = goal.distance;
    });

  auto node = std::make_shared<rclcpp::Node>("action_roundtrip");
  auto client = rclcpp_action::create_client<Drive>(node, name);

  // Discovery: the record the stand-in daemon wrote is what answers this, not a
  // shared-memory handshake.
  check(ros2_shim::graph::action_exists(name), "the action is advertised in the shim graph");
  check(
    ros2_shim::graph::action_type(name) == "ros2_bridge_sim/action/Drive",
    "the advertised action carries its type");
  check(client->action_server_is_ready(), "action_server_is_ready() sees the bridged action");
  check(
    client->wait_for_action_server(std::chrono::milliseconds(500)),
    "wait_for_action_server() returns as soon as the action is there");
  {
    auto missing = rclcpp_action::create_client<Drive>(node, "/test/nothing_here");
    check(
      !missing->wait_for_action_server(std::chrono::milliseconds(200)),
      "wait_for_action_server() times out for an action nobody serves");
  }

  auto goal = std::make_shared<Drive::Goal>();
  goal->distance = 1.25;
  std::vector<double> feedbacks_seen;
  ClientT::SendGoalOptions options;
  options.feedback_callback =
    [&](GoalHandle, std::shared_ptr<const Drive::Feedback> feedback) {
      feedbacks_seen.push_back(feedback->travelled);
    };

  auto goal_future = client->async_send_goal(*goal, options);
  check(waitFor(*node, goal_future, 5.0), "the goal was answered");
  GoalHandle handle = goal_future.get();
  check(handle != nullptr, "an accepted goal comes back as a handle");

  WrappedResult wrapped;
  bool got_result = false;
  if (handle) {
    // Any of the three running states, because this stand-in accepts, drives and
    // finishes inside the one spin that delivered the acceptance: by the time
    // the caller looks, the handle has already been moved on by the feedback
    // (EXECUTING) or by the result (SUCCEEDED). What the check is worth is that
    // the handle is not STATUS_UNKNOWN - that the client followed the goal
    // through the ack it read, rather than handing back a handle it never
    // updated.
    const int8_t status = handle->get_status();
    check(
      status == action_msgs::msg::GoalStatus::STATUS_ACCEPTED ||
      status == action_msgs::msg::GoalStatus::STATUS_EXECUTING ||
      status == action_msgs::msg::GoalStatus::STATUS_SUCCEEDED,
      "the handle tracks the goal past acceptance");
    auto result_future = client->async_get_result(handle);
    check(waitFor(*node, result_future, 5.0), "the result arrived");
    if (result_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      wrapped = result_future.get();
      got_result = true;
    }
  }

  check(seen_goals == 1, "the stand-in daemon saw exactly one goal");
  check(seen_distance == 1.25, "the goal's distance survived shim -> CDR");
  check(feedbacks_seen.size() == 2, "both feedback messages reached the caller");
  if (feedbacks_seen.size() == 2) {
    check(
      feedbacks_seen[0] == 0.5 && feedbacks_seen[1] == 1.0,
      "the feedback carried the values the robot sent");
  }
  check(got_result, "the caller was handed a result");
  if (got_result) {
    check(wrapped.code == rclcpp_action::ResultCode::SUCCEEDED, "the result code is SUCCEEDED");
    check(
      wrapped.result != nullptr && wrapped.result->travelled == 1.25 && wrapped.result->success,
      "the result's fields survived CDR -> shim");
  }
  check(remote->failures() == 0, "every payload converted");

  // -------------------------------------------------------------------------
  // A goal the robot refuses: answered on the ack channel, not left to time out.
  // -------------------------------------------------------------------------
  remote->stop();
  {
    Script reject;
    reject.accept = false;
    reject.cancel_accepted = false;
    auto rejecting = std::make_unique<RemoteAction<Drive>>(
      name, "ros2_bridge_sim/action/Drive", reject, nullptr);
    auto refusing_client = rclcpp_action::create_client<Drive>(node, name);
    check(
      refusing_client->wait_for_action_server(std::chrono::milliseconds(500)),
      "the refusing action is discovered");
    auto future = refusing_client->async_send_goal(*goal);
    check(waitFor(*node, future, 5.0), "a refused goal is still answered");
    // Guarded rather than read outright: a future nobody resolves blocks
    // forever, and a test that hangs tells you less than one that fails.
    if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      check(future.get() == nullptr, "a refused goal comes back as 'no handle'");
    }
    rejecting->stop();
  }

  // -------------------------------------------------------------------------
  // Cancel: accepted by the robot, and the goal then ends as CANCELED.
  // -------------------------------------------------------------------------
  {
    Script slowly;
    slowly.feedback_count = 0;
    // A cancel has to arrive while the goal is still running, so the stand-in
    // daemon keeps it open until then.
    slowly.hold_result = true;
    auto cancelling = std::make_unique<RemoteAction<Drive>>(
      name, "ros2_bridge_sim/action/Drive", slowly, nullptr);
    auto cancelling_client = rclcpp_action::create_client<Drive>(node, name);
    check(
      cancelling_client->wait_for_action_server(std::chrono::milliseconds(500)),
      "the cancellable action is discovered");
    auto goal_future = cancelling_client->async_send_goal(*goal);
    check(waitFor(*node, goal_future, 5.0), "the cancellable goal was answered");
    GoalHandle handle = goal_future.get();
    check(handle != nullptr, "the cancellable goal was accepted");
    if (handle) {
      auto cancel_future = cancelling_client->async_cancel_goal(handle);
      check(waitFor(*node, cancel_future, 5.0), "the cancel was answered");
      check(cancel_future.get() == rclcpp_action::CancelResponse::ACCEPT, "the robot accepted the cancel");
      auto result_future = cancelling_client->async_get_result(handle);
      check(waitFor(*node, result_future, 5.0), "the canceled goal has a result");
      if (result_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const WrappedResult & canceled = result_future.get();
        check(
          canceled.code == rclcpp_action::ResultCode::CANCELED,
          "a canceled goal reports CANCELED, not SUCCEEDED");
      }
    }
    check(cancelling->cancels() == 1, "the stand-in daemon saw exactly one cancel");

    // A cancel for a goal that has already finished is refused by the server,
    // the way the shim's own server refuses an unknown goal id: there is no
    // handle left to run a cancel callback on.
    if (handle) {
      auto late_future = cancelling_client->async_cancel_goal(handle);
      check(waitFor(*node, late_future, 5.0), "a cancel for a finished goal is answered");
      check(
        late_future.get() == rclcpp_action::CancelResponse::REJECT,
        "a cancel for a finished goal is refused");
    }
    check(cancelling->cancels() == 2, "the stand-in daemon saw both cancels");
    cancelling->stop();
  }

  // -------------------------------------------------------------------------
  // The advertisement goes away with the bridge.
  // -------------------------------------------------------------------------
  {
    Script simple;
    auto temporary = std::make_unique<RemoteAction<Drive>>(
      name, "ros2_bridge_sim/action/Drive", simple, nullptr);
    auto watching = rclcpp_action::create_client<Drive>(node, name);
    check(
      watching->wait_for_action_server(std::chrono::milliseconds(500)),
      "the action is discoverable while the bridge serves it");
    temporary->stop();
    check(!ros2_shim::graph::action_exists(name), "the record is gone once the bridge stops");
    check(!watching->action_server_is_ready(), "and the client stops claiming readiness");
  }

  std::printf("\n%d checks, %d failure(s) - %s\n", g_checks, g_failures,
    g_failures == 0 ? "ALL CHECKS PASSED" : "THERE WERE FAILURES");
  return g_failures == 0 ? 0 : 1;
}
