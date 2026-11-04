#include "ros2_bridge/action_proxy.hpp"

#include <cstdio>
#include <cstring>

#include "ros2_bridge/shm_topic.hpp"
#include "ros2_shim/graph.hpp"

namespace ros2_bridge
{
namespace
{

// True when a process on this machine serves the action itself. Action
// records are stored fully qualified (`compute_path_to_pose` as
// `/compute_path_to_pose`), and so is the name the agent advertises, so the
// comparison is direct.
bool local_action_exists(const std::string & name)
{
  for (const auto & entry : ros2_shim::graph::list_actions()) {
    if (entry.name == name && entry.source == "local") {
      return true;
    }
  }
  return false;
}

// Joins the follower threads that have finished, so a long lived daemon does
// not accumulate one per goal ever sent.
void reapFollowers(const std::shared_ptr<ActionProxy> & action)
{
  for (auto it = action->followers.begin(); it != action->followers.end(); ) {
    if (!it->done->load()) {
      ++it;
      continue;
    }
    if (it->thread.joinable()) {
      it->thread.join();
    }
    it = action->followers.erase(it);
  }
}

// The two shapes every message on these channels has in front of it: a goal
// id, then the payload. The ack channels carry the single byte the shim's
// protocol answers a goal or a cancel with.
void publishFlag(
  const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id,
  std::uint8_t value)
{
  std::vector<std::uint8_t> out(sizeof(id) + 1);
  std::memcpy(out.data(), &id, sizeof(id));
  out[sizeof(id)] = value;
  if (!channel->publish(out.data(), out.size())) {
    std::fprintf(stderr, "[bridge] the answer for goal %llu does not fit its slot\n",
      static_cast<unsigned long long>(id));
  }
}

void publishPayload(
  const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id,
  const ros2_grpc_sdk::Bytes & payload)
{
  std::vector<std::uint8_t> out(sizeof(id) + payload.size());
  std::memcpy(out.data(), &id, sizeof(id));
  if (!payload.empty()) {
    std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
  }
  if (!channel->publish(out.data(), out.size())) {
    std::fprintf(
      stderr, "[bridge] a %zu byte payload for goal %llu does not fit its slot\n",
      payload.size(), static_cast<unsigned long long>(id));
  }
}

// A result is a payload with the result's own code in front of it - the
// shim's `rclcpp_action::Client` reads that byte to decide between
// SUCCEEDED, ABORTED and CANCELED (see rclcpp_action/types.hpp).
void publishResult(
  const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id,
  ros2_grpc_sdk::GoalStatus status, const ros2_grpc_sdk::Bytes & payload)
{
  std::vector<std::uint8_t> out(sizeof(id) + 1 + payload.size());
  std::memcpy(out.data(), &id, sizeof(id));
  out[sizeof(id)] = static_cast<std::uint8_t>(shim_result_code(status));
  if (!payload.empty()) {
    std::memcpy(out.data() + sizeof(id) + 1, payload.data(), payload.size());
  }
  if (!channel->publish(out.data(), out.size())) {
    std::fprintf(
      stderr, "[bridge] a result for goal %llu does not fit its slot\n",
      static_cast<unsigned long long>(id));
  }
}

}  // namespace

ActionProxyService::ActionProxyService(ActionProxyContext context)
: context_(std::move(context))
{
}

bool ActionProxyService::sync()
{
  if (!context_.enabled) {
    return true;
  }
  std::vector<ros2_grpc_sdk::ActionInfo> actions;
  // `only_active`: an action the agent knows about but nobody serves there
  // has nothing to answer a goal with, and advertising it here would make a
  // local caller's wait_for_action_server() say yes to a name that can only
  // fail.
  const ros2_grpc_sdk::Status status =
    context_.client->listActions(actions, std::string(), true);
  if (!status.ok()) {
    if (context_.report_scan_failure) {
      context_.report_scan_failure("listActions", status.message());
    }
    return false;
  }
  if (context_.clear_scan_failure) {
    context_.clear_scan_failure("listActions");
  }

  for (const auto & action : actions) {
    if (action.name.empty()) {
      continue;
    }
    // Qualified before it is used for anything: the registry keeps action
    // names the way ROS2 tools print them (`/sim/drive`), and the six
    // channels are opened here from the same name a local client will resolve
    // its own through `Node::get_channel`. An agent that advertised
    // `sim/drive` would otherwise get `_goal` channels under `sim/drive/...`
    // while the client reads `/sim/drive/...`, and the goal would be
    // published into a mapping nobody has open.
    const std::string name = ros2_shim::graph::qualified_action_name(action.name);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (actions_.count(name) > 0) {
        continue;
      }
    }
    if (local_action_exists(name)) {
      continue;
    }
    if (context_.filters != nullptr && !context_.filters->empty() &&
      !matchesAny(name, *context_.filters))
    {
      continue;
    }
    const ActionCodec * codec = find_action_codec(action.type);
    if (codec == nullptr) {
      bool already_warned = false;
      if (context_.warned_types) {
        std::lock_guard<std::mutex> lock(mutex_);
        already_warned = context_.warned_types->count(action.type) > 0;
        if (!already_warned) {
          context_.warned_types->insert(action.type);
        }
      }
      if (!already_warned) {
        std::printf(
          "[bridge] skipping '%s': no codec for action type '%s'\n",
          name.c_str(), action.type.c_str());
      }
      continue;
    }
    ros2_grpc_sdk::ActionInfo qualified = action;
    qualified.name = name;
    startAction(qualified, codec);
  }
  return true;
}

void ActionProxyService::startAction(
  const ros2_grpc_sdk::ActionInfo & info, const ActionCodec * codec)
{
  auto action = std::make_shared<ActionProxy>();
  action->name = info.name;
  action->type = canonical_action_type(info.type);
  action->codec = codec;

  std::string error;
  // Created and held, not opened, for the same reason the service channels
  // are: a `Local\` mapping disappears with its last handle, and a caller
  // that sends its first goal an hour from now still has to find these.
  action->goals = create_action_channel(info.name, "goal", &error);
  action->goal_acks = create_action_channel(info.name, "goal_ack", &error);
  action->cancels = create_action_channel(info.name, "cancel", &error);
  action->cancel_acks = create_action_channel(info.name, "cancel_ack", &error);
  action->feedbacks = create_action_channel(info.name, "feedback", &error);
  action->results = create_action_channel(info.name, "result", &error);
  if (!action->goals || !action->goal_acks || !action->cancels || !action->cancel_acks ||
    !action->feedbacks || !action->results)
  {
    std::fprintf(
      stderr, "[bridge] cannot create the channels for action '%s': %s\n",
      info.name.c_str(), error.c_str());
    return;
  }

  // Positioned at the head of both streams the daemon reads, the way
  // `rclcpp_action::Server` positions its own: the ring outlives the process
  // that wrote it, so a daemon that started in front of a leftover goal would
  // drive the robot towards wherever the last run's caller had asked - and
  // answer a goal nobody sent. The four channels the daemon writes need no
  // positioning; it is the only writer of those.
  action->goals->seek_to_latest();
  action->cancels->seek_to_latest();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (actions_.count(info.name) > 0) {
      return;
    }
    actions_[info.name] = action;
  }

  // Advertised as long as the bridge can answer, which is what makes a
  // caller's wait_for_action_server() say yes instead of timing out.
  const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();
  ros2_shim::graph::ProcessRegistry::instance().add_action(
    info.name, action->type, "remote:" + peer);

  if (context_.once) {
    return;
  }
  action->running = true;
  action->worker = std::thread([this, action]() { actionLoop(action); });
  std::printf(
    "[bridge] serving action %s [%s] on shm '%s'\n", info.name.c_str(), action->type.c_str(),
    channel_name(action_channel(info.name, "goal")).c_str());
}

void ActionProxyService::actionLoop(const std::shared_ptr<ActionProxy> & action)
{
  const ActionCodec & codec = *action->codec;
  std::vector<std::uint8_t> buffer(action->goals->slot_size());
  while ((!context_.running || context_.running()) && action->running) {
    reapFollowers(action);

    std::size_t size = 0;
    // Long enough to stay off the CPU, short enough that a cancel does not
    // sit behind a goal that never comes.
    if (action->goals->subscribe(buffer.data(), buffer.size(), &size, 50)) {
      if (size > sizeof(std::uint64_t)) {
        std::uint64_t id = 0;
        std::memcpy(&id, buffer.data(), sizeof(id));
        handleGoal(
          action, codec, id,
          ros2_grpc_sdk::Bytes(buffer.begin() + sizeof(id), buffer.begin() + size));
      }
    }

    size = 0;
    if (!action->cancels->subscribe(buffer.data(), buffer.size(), &size, 50)) {
      continue;
    }
    if (size < sizeof(std::uint64_t)) {
      continue;
    }
    std::uint64_t id = 0;
    std::memcpy(&id, buffer.data(), sizeof(id));
    handleCancel(action, id);
  }
}

void ActionProxyService::handleGoal(
  const std::shared_ptr<ActionProxy> & action, const ActionCodec & codec,
  std::uint64_t id, const ros2_grpc_sdk::Bytes & goal_shim)
{
  ros2_grpc_sdk::Bytes goal_cdr;
  if (!codec.goal_to_cdr(goal_shim, goal_cdr)) {
    action->failures.fetch_add(1);
    std::fprintf(
      stderr, "[bridge] '%s': undecodable goal from shared memory\n", action->name.c_str());
    publishFlag(action->goal_acks, id, 0);
    return;
  }

  std::string remote_id;
  const ros2_grpc_sdk::Status status = context_.client->sendActionGoal(
    action->name, action->type, goal_cdr, remote_id, "cdr");
  if (!status.ok()) {
    action->failures.fetch_add(1);
    std::fprintf(
      stderr, "[bridge] '%s': sending the goal failed: %s\n", action->name.c_str(),
      status.toString().c_str());
    publishFlag(action->goal_acks, id, 0);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(action->mutex);
    action->remote_ids[id] = remote_id;
  }
  action->goals_sent.fetch_add(1);
  // The acceptance is the robot's: the agent answered the SendActionGoal RPC
  // with a goal id, so the goal runs.
  publishFlag(action->goal_acks, id, 1);

  auto done = std::make_shared<std::atomic<bool>>(false);
  ActionProxy::Follower follower;
  follower.done = done;
  follower.thread = std::thread(
    [this, action, id, remote_id, done]() {
      followGoal(action, id, remote_id);
      done->store(true);
    });
  action->followers.push_back(std::move(follower));
}

void ActionProxyService::followGoal(
  const std::shared_ptr<ActionProxy> & action, std::uint64_t id,
  const std::string & remote_id)
{
  const ActionCodec & codec = *action->codec;
  const ros2_grpc_sdk::Status status = context_.client->followActionGoal(
    action->name, remote_id,
    [&](const ros2_grpc_sdk::ActionFeedback & message) {
      const bool cdr = message.encoding.empty() || message.encoding == "cdr";
      if (message.terminal()) {
        ros2_grpc_sdk::Bytes result_shim;
        if (!cdr || !codec.result_to_shim(message.result, result_shim)) {
          // A robot that ends a goal without a result payload - a cancel
          // before the goal produced anything is the usual reason - still has
          // to be answered here. Dropping the message would leave the caller
          // waiting out its whole timeout for a goal that is already over, so
          // a default-constructed result goes back instead, which is what
          // upstream hands a caller in the same position. Counted, because
          // the payload really was lost.
          action->failures.fetch_add(1);
          std::fprintf(
            stderr, "[bridge] '%s': no usable result for goal %llu (%zu bytes, status %d%s%s); "
            "answering with an empty one\n",
            action->name.c_str(), static_cast<unsigned long long>(id), message.result.size(),
            static_cast<int>(message.status), message.error.empty() ? "" : ", ",
            message.error.c_str());
          result_shim = codec.default_result;
        }
        publishResult(action->results, id, message.status, result_shim);
        action->results_sent.fetch_add(1);
        return false;  // the goal is over; the stream has nothing left to say
      }
      ros2_grpc_sdk::Bytes feedback_shim;
      if (!cdr || !codec.feedback_to_shim(message.feedback, feedback_shim)) {
        action->failures.fetch_add(1);
        return true;  // one lost feedback message must not kill the goal
      }
      publishPayload(action->feedbacks, id, feedback_shim);
      action->feedbacks_sent.fetch_add(1);
      return true;
    });

  if (!status.ok()) {
    // No result was delivered, so the caller is left to time out on its own
    // - there is nothing to hand it, and inventing a result would be worse
    // than an honest silence. Said out loud so the reason is in the log.
    action->failures.fetch_add(1);
    std::fprintf(
      stderr, "[bridge] '%s': following the goal failed: %s\n", action->name.c_str(),
      status.toString().c_str());
  }
  {
    std::lock_guard<std::mutex> lock(action->mutex);
    action->remote_ids.erase(id);
  }
}

void ActionProxyService::handleCancel(
  const std::shared_ptr<ActionProxy> & action, std::uint64_t id)
{
  std::string remote_id;
  {
    std::lock_guard<std::mutex> lock(action->mutex);
    const auto it = action->remote_ids.find(id);
    if (it != action->remote_ids.end()) {
      remote_id = it->second;
    }
  }
  if (remote_id.empty()) {
    // Either the goal never existed or it has already finished. The shim's
    // own server answers REJECT in both cases rather than staying silent,
    // which would leave the caller waiting out its whole timeout.
    publishFlag(action->cancel_acks, id, 0);
    return;
  }
  const ros2_grpc_sdk::Status status =
    context_.client->cancelActionGoal(action->name, remote_id);
  if (!status.ok()) {
    action->failures.fetch_add(1);
    std::fprintf(
      stderr, "[bridge] '%s': the cancel was not delivered: %s\n", action->name.c_str(),
      status.toString().c_str());
  }
  publishFlag(action->cancel_acks, id, status.ok() ? 1 : 0);
}

std::size_t ActionProxyService::action_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return actions_.size();
}

void ActionProxyService::append_report(std::vector<std::string> & lines) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto & entry : actions_) {
    const auto & action = entry.second;
    char buffer[256];
    std::snprintf(
      buffer, sizeof(buffer),
      "  a %-26s %-34s goals=%-6llu feedback=%-8llu results=%-6llu failed=%llu",
      action->name.c_str(), action->type.c_str(),
      static_cast<unsigned long long>(action->goals_sent.load()),
      static_cast<unsigned long long>(action->feedbacks_sent.load()),
      static_cast<unsigned long long>(action->results_sent.load()),
      static_cast<unsigned long long>(action->failures.load()));
    lines.push_back(buffer);
  }
}

void ActionProxyService::stop()
{
  std::vector<std::shared_ptr<ActionProxy>> actions;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & entry : actions_) {
      entry.second->running = false;
      actions.push_back(entry.second);
    }
  }
  const std::string & peer = context_.peer != nullptr ? *context_.peer : std::string();
  // An action's followers are streaming gRPC calls, so they have to be out
  // of the way before the client goes - the same rule the service workers
  // are joined under, and for the same reason.
  for (const auto & action : actions) {
    if (action->worker.joinable()) {
      action->worker.join();
    }
    for (auto & follower : action->followers) {
      if (follower.thread.joinable()) {
        follower.thread.join();
      }
    }
    ros2_shim::graph::ProcessRegistry::instance().remove_action(
      action->name, "remote:" + peer);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  actions_.clear();
}

}  // namespace ros2_bridge
