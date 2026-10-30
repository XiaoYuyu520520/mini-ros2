#ifndef RCLCPP_ACTION__RCLCPP_ACTION_HPP_
#define RCLCPP_ACTION__RCLCPP_ACTION_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "action_msgs/msg/goal_status.hpp"
#include "action_msgs/msg/goal_status_array.hpp"
#include "rcl_action/action_server.h"
#include "rclcpp/node.hpp"
#include "rclcpp_action/types.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

namespace rclcpp_action
{

namespace detail
{

// 观察开关：ROS2_SHIM_ACTION_TRACE=1 的时候，六条 action channel 上每一次
// 收发都往 stderr 打一行。目的是能分辨"冷启动第一次 goal 静默悬挂"到底丢在
// 哪一段：客户端没发出 goal、服务端没收到 goal、服务端没回 ack、结果的
// publish 失败、还是客户端读到了 id 对不上的报文。默认关闭，零开销。
inline bool action_trace_enabled()
{
  static const bool on = std::getenv("ROS2_SHIM_ACTION_TRACE") != nullptr;
  return on;
}

inline double action_trace_clock()
{
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

inline unsigned long action_trace_pid()
{
#ifdef _WIN32
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

inline void trace_msg(
  const char * role, const std::string & action, const std::string & text)
{
  if (!action_trace_enabled()) {
    return;
  }
  std::fprintf(
    stderr, "[act %-6s pid=%lu %9.3f] %-24s %s\n", role, action_trace_pid(),
    action_trace_clock(), action.c_str(), text.c_str());
  std::fflush(stderr);
}

inline void send_message(
  const std::shared_ptr<shm_transport::ShmChannel> & channel, uint64_t id,
  const std::vector<uint8_t> & payload)
{
  std::vector<uint8_t> out(sizeof(id) + payload.size());
  std::memcpy(out.data(), &id, sizeof(id));
  if (!payload.empty()) {
    std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
  }
  if (!channel->publish(out.data(), out.size())) {
    std::fprintf(
      stderr, "[act   !!  pid=%lu] publish failed: %zu bytes > slot_size %u on %s\n",
      action_trace_pid(), out.size(), channel->slot_size(), channel->name().c_str());
    std::fflush(stderr);
  }
}

inline bool read_id(const std::vector<uint8_t> & buffer, size_t size, uint64_t & id)
{
  if (size < sizeof(uint64_t)) {
    return false;
  }
  std::memcpy(&id, buffer.data(), sizeof(id));
  return true;
}

inline GoalUUID make_uuid(uint64_t id)
{
  GoalUUID uuid{};
  std::memcpy(uuid.data(), &id, sizeof(id) < 16 ? sizeof(id) : 16);
  return uuid;
}

// The `action_msgs/msg/GoalStatus` code a finished goal reports.
//
// rclcpp_action's own `ResultCode` is the wire's four-value enum and the message
// has six terminals-plus-two it could be spelled with; this is the one place the
// two vocabularies are mapped, so `<action>/_action/status` and the result the
// caller reads cannot disagree about how a goal ended.
inline int8_t goal_status_for(ResultCode code)
{
  switch (code) {
    case ResultCode::SUCCEEDED: return action_msgs::msg::GoalStatus::STATUS_SUCCEEDED;
    case ResultCode::CANCELED: return action_msgs::msg::GoalStatus::STATUS_CANCELED;
    case ResultCode::ABORTED: return action_msgs::msg::GoalStatus::STATUS_ABORTED;
    default: break;
  }
  return action_msgs::msg::GoalStatus::STATUS_UNKNOWN;
}

// The action's own ROS2 type name, e.g. "nav2_msgs/action/NavigateToPose".
//
// The generated headers name the three parts (`..._Goal`, `_Feedback`,
// `_Result`) and nothing else, so it is derived from the goal - the one part
// every action is guaranteed to have. It ends up in the graph registry, where
// it is what `ros2 action list -t` and `ros2 action type` print.
template<class ActionT>
inline std::string action_type_name()
{
  const std::string goal_type = ActionT::Goal::ros2_message_type;
  const std::string suffix = "_Goal";
  if (goal_type.size() > suffix.size() &&
    goal_type.compare(goal_type.size() - suffix.size(), suffix.size(), suffix) == 0)
  {
    return goal_type.substr(0, goal_type.size() - suffix.size());
  }
  return goal_type;
}

}  // namespace detail

// ---------------------------------------------------------------- server side
template<class ActionT>
class Server;

template<class ActionT>
class ServerGoalHandle : public std::enable_shared_from_this<ServerGoalHandle<ActionT>>
{
public:
  using SharedPtr = std::shared_ptr<ServerGoalHandle<ActionT>>;
  using Goal = typename ActionT::Goal;
  using Feedback = typename ActionT::Feedback;
  using Result = typename ActionT::Result;

  ServerGoalHandle(
    std::shared_ptr<Server<ActionT>> server, uint64_t id, std::shared_ptr<const Goal> goal)
  : server_(server), id_(id), goal_(std::move(goal))
  {
  }

  bool is_active() const {return active_;}
  bool is_canceling() const {return canceling_;}
  GoalUUID get_goal_id() const {return detail::make_uuid(id_);}
  std::shared_ptr<const Goal> get_goal() const {return goal_;}
  const Goal & goal() const {return *goal_;}

  void succeed(std::shared_ptr<Result> result);
  void abort(std::shared_ptr<Result> result);
  void canceled(std::shared_ptr<Result> result);
  void publish_feedback(std::shared_ptr<Feedback> feedback);

  void set_canceling(bool value) {canceling_ = value;}
  uint64_t id() const {return id_;}
  void deactivate() {active_ = false;}

private:
  std::shared_ptr<Server<ActionT>> server_;
  uint64_t id_;
  std::shared_ptr<const Goal> goal_;
  bool active_ = true;
  bool canceling_ = false;
};

template<class ActionT>
class Server : public std::enable_shared_from_this<Server<ActionT>>
{
public:
  using SharedPtr = std::shared_ptr<Server<ActionT>>;
  using UniquePtr = std::unique_ptr<Server<ActionT>>;
  using Goal = typename ActionT::Goal;
  using Feedback = typename ActionT::Feedback;
  using Result = typename ActionT::Result;
  using GoalHandle = ServerGoalHandle<ActionT>;
  using GoalCallback = std::function<GoalResponse(const GoalUUID &, std::shared_ptr<const Goal>)>;
  using CancelCallback = std::function<CancelResponse(std::shared_ptr<GoalHandle>)>;
  using AcceptedCallback = std::function<void(std::shared_ptr<GoalHandle>)>;

  Server(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base,
    const std::string & action_name, GoalCallback goal_callback, CancelCallback cancel_callback,
    AcceptedCallback accepted_callback,
    rcl_action_server_options_t = rcl_action_server_get_default_options(),
    rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
  : node_(node_base ? node_base->node : nullptr), name_(action_name),
    goal_callback_(std::move(goal_callback)), cancel_callback_(std::move(cancel_callback)),
    accepted_callback_(std::move(accepted_callback)),
    callback_group_(std::move(callback_group))
  {
    // `get_private_channel`, not `get_channel`: the node hands the same channel
    // object - and so the same read cursor - to everyone who asks for one name,
    // and a server that shared its goal channel with a client of the same name
    // would lose whichever of the two messages the other polled first. See the
    // note on `Node::get_private_channel`.
    goal_channel_ = node_->get_private_channel(name_ + "/_goal");
    ack_channel_ = node_->get_private_channel(name_ + "/_goal_ack");
    cancel_channel_ = node_->get_private_channel(name_ + "/_cancel");
    cancel_ack_channel_ = node_->get_private_channel(name_ + "/_cancel_ack");
    feedback_channel_ = node_->get_private_channel(name_ + "/_feedback");
    result_channel_ = node_->get_private_channel(name_ + "/_result");
    // Start at the head of the stream, like every volatile subscription here
    // does (`Subscription::skip_backlog`): the ring outlives the process that
    // wrote it, so a fresh server would otherwise be handed the goals of the
    // previous run and answer them - with a REJECT, because at this point it is
    // still inactive. See the same note in the client constructor for what that
    // costs when the ids line up.
    const uint64_t goal_skipped = goal_channel_->seek_to_latest();
    const uint64_t cancel_skipped = cancel_channel_->seek_to_latest();
    detail::trace_msg(
      "SERVER", name_,
      "created, skipped goal=" + std::to_string(goal_skipped) +
      " cancel=" + std::to_string(cancel_skipped));
    goal_buffer_.resize(16u * 1024 * 1024);
    cancel_buffer_.resize(64);
    // Advertised like a service is (see rclcpp/node.hpp's create_service): the
    // shim has no DDS discovery, so this record is the only thing that lets a
    // client waiting on `wait_for_action_server()` - nav2's behavior tree
    // nodes do, once per BT - learn that this name can be sent a goal.
    ros2_shim::graph::ProcessRegistry::instance().add_action(
      name_, detail::action_type_name<ActionT>());
    // The seventh channel, and the only one that is an ordinary topic rather
    // than half of a private protocol: `<action>/_action/status` carries an
    // `action_msgs/msg/GoalStatusArray` saying where every goal this server
    // knows about has got to. rcl_action publishes it from the same places this
    // class does - a goal accepted, a goal whose state changed, a goal that
    // finished - and it is what `nav2_waypoint_cycle` subscribes to instead of
    // blocking on `async_get_result`. Without it an action server is one a
    // mission script cannot drive: `ros2 topic list` does not even carry the
    // name, so the script waits on a subscription that never has a publisher.
    //
    // `transient_local` with a depth of one is upstream's
    // `rcl_action_qos_profile_status_default`; the shim's transport has no
    // reliability or durability semantics, but the flag is what makes a
    // subscriber that arrives after the last transition still see it.
    status_publisher_ = node_->create_publisher<action_msgs::msg::GoalStatusArray>(
      name_ + "/_action/status", rclcpp::QoS(1).transient_local());
  }

  // Withdrawn with the server: a client must stop advertising readiness for an
  // action nobody serves any more. (Two servers for one name in one process
  // share the record, the same way two services of one name do; the second
  // removal is then a no-op for the first.)
  ~Server()
  {
    ros2_shim::graph::ProcessRegistry::instance().remove_action(name_);
  }

  void register_with_node()
  {
    std::weak_ptr<Server<ActionT>> weak = this->shared_from_this();
    // The poll goes in the group the server was created with, which is what
    // decides the thread that runs it: `nav2::SimpleActionServer` hands it a
    // group it spins itself, and every goal this server then handles - and the
    // execute callback behind it - runs there rather than on the node's.
    node_->add_pollable([weak]() {
        if (auto self = weak.lock()) {
          self->poll();
        }
      }, callback_group_);
  }

  const std::string & get_name() const {return name_;}

  void poll()
  {
    for (;;) {
      size_t size = 0;
      if (!goal_channel_->subscribe(goal_buffer_.data(), goal_buffer_.size(), &size, 0)) {
        break;
      }
      uint64_t id = 0;
      if (!detail::read_id(goal_buffer_, size, id)) {
        continue;
      }
      auto goal = std::make_shared<Goal>();
      if (!ros2_shim::deserialize(
          goal_buffer_.data() + sizeof(id), size - sizeof(id), *goal))
      {
        detail::trace_msg(
          "SERVER", name_,
          "goal id=" + std::to_string(id) + " dropped: deserialize failed, " +
          std::to_string(size) + " bytes");
        continue;
      }
      detail::trace_msg(
        "SERVER", name_, "recv goal id=" + std::to_string(id) + " (" +
        std::to_string(size) + " bytes)");
      const auto response = goal_callback_(detail::make_uuid(id), goal);
      std::vector<uint8_t> ack(1, response == GoalResponse::REJECT ? 0 : 1);
      detail::send_message(ack_channel_, id, ack);
      detail::trace_msg(
        "SERVER", name_,
        "sent ack id=" + std::to_string(id) +
        (response == GoalResponse::REJECT ? " REJECT" : " ACCEPT"));
      if (response == GoalResponse::REJECT) {
        continue;
      }
      auto handle = std::make_shared<GoalHandle>(this->shared_from_this(), id, goal);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        handles_[id] = handle;
        record_status_locked(id, action_msgs::msg::GoalStatus::STATUS_ACCEPTED);
      }
      publish_status();
      // rcl_action moves a goal to EXECUTING as soon as execution starts, and
      // hands the server its own signal for that. This port has none: the
      // callback below is where nav2's SimpleActionServer runs the execute
      // callback from, so the handle it is given *is* execution starting.
      {
        std::lock_guard<std::mutex> lock(mutex_);
        record_status_locked(id, action_msgs::msg::GoalStatus::STATUS_EXECUTING);
      }
      publish_status();
      accepted_callback_(handle);
    }

    for (;;) {
      size_t size = 0;
      if (!cancel_channel_->subscribe(cancel_buffer_.data(), cancel_buffer_.size(), &size, 0)) {
        break;
      }
      uint64_t id = 0;
      if (!detail::read_id(cancel_buffer_, size, id)) {
        continue;
      }
      detail::trace_msg("SERVER", name_, "recv cancel id=" + std::to_string(id));
      std::shared_ptr<GoalHandle> handle;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(id);
        if (it != handles_.end()) {
          handle = it->second;
        }
      }
      if (!handle) {
        // The goal already finished, so there is no cancel callback left to
        // run. Answer anyway: upstream replies with ERROR_UNKNOWN_GOAL_ID
        // rather than staying silent, and a silent server would leave a
        // caller that waits on the reply stuck for its whole timeout.
        const std::vector<uint8_t> unknown(1, 0);
        detail::send_message(cancel_ack_channel_, id, unknown);
        continue;
      }
      handle->set_canceling(true);
      const auto response = cancel_callback_ ? cancel_callback_(handle) : CancelResponse::REJECT;
      std::vector<uint8_t> ack(1, response == CancelResponse::ACCEPT ? 1 : 0);
      detail::send_message(cancel_ack_channel_, id, ack);
      if (response == CancelResponse::ACCEPT) {
        // Only an accepted cancel moves the goal, which is rcl_action's rule
        // too: a rejected one leaves it executing and says so on the ack.
        // `publish_status` takes `mutex_` itself, so the record is made in a
        // scope of its own rather than under a lock that is still held.
        {
          std::lock_guard<std::mutex> lock(mutex_);
          record_status_locked(id, action_msgs::msg::GoalStatus::STATUS_CANCELING);
        }
        publish_status();
      }
    }
  }

  void publish_feedback(uint64_t id, std::shared_ptr<Feedback> feedback)
  {
    const auto body = ros2_shim::serialize(*feedback);
    detail::trace_msg(
      "SERVER", name_,
      "publish feedback id=" + std::to_string(id) + " (" + std::to_string(body.size()) +
      " bytes)");
    detail::send_message(feedback_channel_, id, body);
  }

  void publish_result(uint64_t id, ResultCode code, std::shared_ptr<Result> result)
  {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>(code));
    const auto body = ros2_shim::serialize(*result);
    payload.insert(payload.end(), body.begin(), body.end());
    detail::trace_msg(
      "SERVER", name_,
      "publish result id=" + std::to_string(id) + " code=" +
      std::to_string(static_cast<int>(code)));
    detail::send_message(result_channel_, id, payload);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      record_status_locked(id, detail::goal_status_for(code));
      handles_.erase(id);
    }
    publish_status();
  }

  // The array `publish_status` fills in: every goal this server has answered,
  // oldest first, with the ones that are over retired after a while.
  void publish_status()
  {
    action_msgs::msg::GoalStatusArray message;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      prune_statuses_locked();
      message.status_list.reserve(statuses_.size());
      for (const auto & entry : statuses_) {
        message.status_list.push_back(entry.second);
      }
    }
    detail::trace_msg(
      "SERVER", name_,
      "publish status (" + std::to_string(message.status_list.size()) + " goals)");
    status_publisher_->publish(message);
  }

private:
  rclcpp::Node * node_;
  std::string name_;
  GoalCallback goal_callback_;
  CancelCallback cancel_callback_;
  AcceptedCallback accepted_callback_;
  // The group the caller asked this server to live in, or null for the node's
  // own. `nav2::SimpleActionServer` passes the one it spins on a thread of its
  // own when it was built with `spin_thread`.
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  std::shared_ptr<shm_transport::ShmChannel> goal_channel_;
  std::shared_ptr<shm_transport::ShmChannel> ack_channel_;
  std::shared_ptr<shm_transport::ShmChannel> cancel_channel_;
  std::shared_ptr<shm_transport::ShmChannel> cancel_ack_channel_;
  std::shared_ptr<shm_transport::ShmChannel> feedback_channel_;
  std::shared_ptr<shm_transport::ShmChannel> result_channel_;
  std::vector<uint8_t> goal_buffer_;
  std::vector<uint8_t> cancel_buffer_;
  std::mutex mutex_;
  std::map<uint64_t, std::shared_ptr<GoalHandle>> handles_;
  rclcpp::Publisher<action_msgs::msg::GoalStatusArray>::SharedPtr status_publisher_;
  // How long a finished goal stays in the status array, and how many entries
  // the array may carry. rcl_action retires a goal once its result has been
  // taken or its `result_timeout` runs out; there is no result bookkeeping here
  // to hang that on, so it is a plain window - long enough that a subscriber
  // that was busy still sees the terminal state, short enough that a long
  // mission does not re-serialise every goal it ever ran on every transition.
  static constexpr double kStatusRetentionSeconds = 10.0;
  static constexpr std::size_t kStatusEntries = 64;
  std::map<uint64_t, action_msgs::msg::GoalStatus> statuses_;
  // When each finished goal finished, on the wall clock - the one clock that
  // keeps running whichever timeline the node's `now()` is on.
  std::map<uint64_t, double> finished_at_;

  static double wall_seconds()
  {
    return std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  // Called with `mutex_` held. The stamp is the moment the goal was accepted
  // and never moves afterwards: that is what rcl_action puts in `GoalInfo`, and
  // it is what a subscriber comparing the stamp against a moment of its own
  // (nav2_waypoint_cycle does exactly that) is entitled to assume.
  void record_status_locked(uint64_t id, int8_t status)
  {
    auto it = statuses_.find(id);
    if (it == statuses_.end()) {
      action_msgs::msg::GoalStatus entry;
      const GoalUUID uuid = detail::make_uuid(id);
      std::memcpy(entry.goal_info.goal_id.uuid.data(), uuid.data(), uuid.size());
      entry.goal_info.stamp = node_->get_clock()->now().to_message();
      it = statuses_.emplace(id, entry).first;
    }
    it->second.status = status;
    if (detail::goal_status_for(ResultCode::SUCCEEDED) == status ||
      detail::goal_status_for(ResultCode::CANCELED) == status ||
      detail::goal_status_for(ResultCode::ABORTED) == status)
    {
      finished_at_[id] = wall_seconds();
    }
  }

  // Called with `mutex_` held.
  void prune_statuses_locked()
  {
    const double now = wall_seconds();
    for (auto it = finished_at_.begin(); it != finished_at_.end();) {
      if (now - it->second <= kStatusRetentionSeconds) {
        ++it;
        continue;
      }
      statuses_.erase(it->first);
      it = finished_at_.erase(it);
    }
    while (statuses_.size() > kStatusEntries && !finished_at_.empty()) {
      // A goal that is still running is never dropped, even if that leaves the
      // array longer than the cap: forgetting one would be a lie about a goal
      // somebody may still be waiting on.
      const auto oldest = finished_at_.begin();
      statuses_.erase(oldest->first);
      finished_at_.erase(oldest);
    }
  }
};

template<class ActionT>
void ServerGoalHandle<ActionT>::succeed(std::shared_ptr<Result> result)
{
  server_->publish_result(id_, ResultCode::SUCCEEDED, result);
  active_ = false;
}

template<class ActionT>
void ServerGoalHandle<ActionT>::abort(std::shared_ptr<Result> result)
{
  server_->publish_result(id_, ResultCode::ABORTED, result);
  active_ = false;
}

template<class ActionT>
void ServerGoalHandle<ActionT>::canceled(std::shared_ptr<Result> result)
{
  server_->publish_result(id_, ResultCode::CANCELED, result);
  active_ = false;
}

template<class ActionT>
void ServerGoalHandle<ActionT>::publish_feedback(std::shared_ptr<Feedback> feedback)
{
  server_->publish_feedback(id_, std::move(feedback));
}

// ---------------------------------------------------------------- client side
template<class ActionT>
class Client;

template<class ActionT>
struct WrappedResult;

template<class ActionT>
class ClientGoalHandle
{
public:
  using SharedPtr = std::shared_ptr<ClientGoalHandle<ActionT>>;
  using Goal = typename ActionT::Goal;
  using Result = typename ActionT::Result;
  using WrappedResult = WrappedResult<ActionT>;

  ClientGoalHandle() = default;
  ClientGoalHandle(uint64_t id, GoalUUID uuid, std::shared_ptr<const Goal> goal)
  : id_(id), uuid_(uuid), goal_(std::move(goal))
  {
  }

  const GoalUUID & get_goal_id() const {return uuid_;}
  std::shared_ptr<const Goal> get_goal() const {return goal_;}
  const Goal & goal() const {return *goal_;}
  uint64_t id() const {return id_;}

  /// Where the goal has got to: one of `action_msgs::msg::GoalStatus`' codes.
  /// `bt_action_node` asks before it decides whether the goal it is about to
  /// replace is still executing, so this moves with the traffic -- ACCEPTED
  /// when the server acknowledges, EXECUTING from the first feedback on, and
  /// one of the three terminal codes when the result lands.
  int8_t get_status() const {return status_;}
  void set_status(int8_t status) {status_ = status;}

  /// When the goal was sent. `async_cancel_goals_before()` needs it: the
  /// cancel behavior tree cancels everything older than a moment ago and must
  /// leave alone a goal that was sent just now.
  const rclcpp::Time & sent_at() const {return sent_at_;}
  void set_sent_at(const rclcpp::Time & sent) {sent_at_ = sent;}

private:
  uint64_t id_ = 0;
  GoalUUID uuid_{};
  std::shared_ptr<const Goal> goal_;
  int8_t status_ = action_msgs::msg::GoalStatus::STATUS_UNKNOWN;
  rclcpp::Time sent_at_{static_cast<int64_t>(0)};
};

template<class ActionT>
struct WrappedResult
{
  using Result = typename ActionT::Result;
  ResultCode code = ResultCode::UNKNOWN;
  std::shared_ptr<Result> result;
  GoalUUID goal_id{};
};

template<class ActionT>
struct SendGoalOptions
{
  std::function<void(std::shared_ptr<ClientGoalHandle<ActionT>>)> goal_response_callback;
  std::function<void(const WrappedResult<ActionT> &)> result_callback;
  std::function<void(
    std::shared_ptr<ClientGoalHandle<ActionT>>,
    std::shared_ptr<const typename ActionT::Feedback>)> feedback_callback;
};

template<class ActionT>
class Client : public std::enable_shared_from_this<Client<ActionT>>
{
public:
  using SharedPtr = std::shared_ptr<Client<ActionT>>;
  using GoalHandle = ClientGoalHandle<ActionT>;
  using Goal = typename ActionT::Goal;
  using Feedback = typename ActionT::Feedback;
  using Result = typename ActionT::Result;
  using GoalHandleSharedPtr = std::shared_ptr<GoalHandle>;
  using WrappedResultT = WrappedResult<ActionT>;
  using SendGoalOptionsT = SendGoalOptions<ActionT>;
  using WrappedResult = WrappedResult<ActionT>;
  using SendGoalOptions = SendGoalOptions<ActionT>;
  using GoalResponseCallback =
    std::function<void(std::shared_ptr<typename ActionT::Goal>)>;

  Client(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base,
    const std::string & action_name, rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
  : node_(node_base ? node_base->node : nullptr), name_(action_name),
    callback_group_(std::move(callback_group))
  {
    // A client gets its own cursors for the same reason the server does: two
    // clients for one action in one process both wait on `_goal_ack`, and on a
    // single shared cursor the first one to poll takes the answer that belonged
    // to the other. Upstream gives every client its own DDS reader and this is
    // the same thing.
    goal_channel_ = node_->get_private_channel(name_ + "/_goal");
    ack_channel_ = node_->get_private_channel(name_ + "/_goal_ack");
    cancel_channel_ = node_->get_private_channel(name_ + "/_cancel");
    cancel_ack_channel_ = node_->get_private_channel(name_ + "/_cancel_ack");
    feedback_channel_ = node_->get_private_channel(name_ + "/_feedback");
    result_channel_ = node_->get_private_channel(name_ + "/_result");
    // This is the half that shows up as "the navigator refused the goal". Goal
    // ids come from a counter that starts at one in every process, and the ring
    // keeps what the previous run left in it - so a client that enters at the
    // ring head reads the *previous* run's ack for id 1 as the answer to its own
    // first goal. If that old goal had been rejected (its server was activating
    // at the time) the new goal is reported rejected although the stack is now
    // up, and the customer-facing symptom is a nav2 that refuses to drive until
    // the machine is idle for long enough to wrap the ring.
    const uint64_t ack_skipped = ack_channel_->seek_to_latest();
    const uint64_t cancel_ack_skipped = cancel_ack_channel_->seek_to_latest();
    const uint64_t feedback_skipped = feedback_channel_->seek_to_latest();
    const uint64_t result_skipped = result_channel_->seek_to_latest();
    detail::trace_msg(
      "CLIENT", name_,
      "created, skipped ack=" + std::to_string(ack_skipped) + " cancel_ack=" +
      std::to_string(cancel_ack_skipped) + " feedback=" + std::to_string(feedback_skipped) +
      " result=" + std::to_string(result_skipped));
    ack_buffer_.resize(64);
    cancel_ack_buffer_.resize(64);
    feedback_buffer_.resize(16u * 1024 * 1024);
    result_buffer_.resize(16u * 1024 * 1024);
  }

  void register_with_node()
  {
    std::weak_ptr<Client<ActionT>> weak = this->shared_from_this();
    // Like the server, the poll belongs to the group the client was created
    // with. `nav2_behavior_tree`'s action nodes and `nav2_waypoint_follower`
    // hand it the group their own executor spins while they wait for a result,
    // so a client that landed in the node's group instead would leave them
    // waiting for an answer nothing was reading.
    node_->add_pollable([weak]() {
        if (auto self = weak.lock()) {
          self->poll();
        }
      }, callback_group_);
  }

  const std::string & get_name() const {return name_;}

  /// True when somebody serves this action - a server on this machine, or the
  /// gRPC bridge standing in for one on the robot.
  ///
  /// This used to answer `true` unconditionally, on the grounds that a shared
  /// memory channel cannot be asked. The graph registry can be asked, though,
  /// and the difference shows up exactly when it matters: `nav2_behavior_tree`'s
  /// `BtActionNode::on_init()` treats "not ready" as a reason to refuse to run
  /// the tree, so a client that lies about readiness makes the failure appear
  /// later and somewhere else (a goal that is sent into a ring nobody reads and
  /// times out), while a client that answers honestly reports it where the
  /// caller can act on it.
  bool action_server_is_ready() const {return ros2_shim::graph::action_exists(name_);}

  template<class RepT, class PeriodT>
  bool wait_for_action_server(const std::chrono::duration<RepT, PeriodT> & timeout)
  {
    return ros2_shim::graph::wait_for_action(name_, timeout);
  }

  /// Upstream's "wait forever" overload. Answered through the same registry,
  /// so it is safe to call from a node that will be configured before the
  /// server's process has started.
  bool wait_for_action_server() {return action_server_is_ready();}

  std::shared_future<GoalHandleSharedPtr> async_send_goal(
    const Goal & goal, const SendGoalOptionsT & options = SendGoalOptionsT())
  {
    // The counter is shared by every client in the process, not one per client:
    // goal ids have to be unique against the *channel*, and the channel belongs
    // to an action name, so two clients for one action that each counted from
    // one would name their first goal alike and a server could not tell them
    // apart. Upstream gets this from the 16 byte UUID.
    static std::atomic<uint64_t> next_goal_id{0};
    const uint64_t id = ++next_goal_id;
    auto payload = ros2_shim::serialize(goal);
    detail::trace_msg(
      "CLIENT", name_,
      "send goal id=" + std::to_string(id) + " (" + std::to_string(payload.size()) +
      " bytes)");
    detail::send_message(goal_channel_, id, payload);

    auto handle = std::make_shared<GoalHandle>(id, detail::make_uuid(id),
      std::make_shared<Goal>(goal));
    handle->set_sent_at(node_ ? node_->now() : rclcpp::Time(static_cast<int64_t>(0)));
    auto promise = std::make_shared<std::promise<GoalHandleSharedPtr>>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      goals_[id] = Pending{
        handle, options, promise, std::make_shared<std::atomic<bool>>(false)};
    }
    return promise->get_future().share();
  }

  std::shared_future<WrappedResultT> async_get_result(GoalHandleSharedPtr handle)
  {
    auto promise = std::make_shared<std::promise<WrappedResultT>>();
    if (!handle) {
      promise->set_value(WrappedResultT{});
      return promise->get_future().share();
    }
    // A result may already have arrived while the caller was waiting for the
    // goal response, so hand out the buffered one instead of waiting for a
    // second message that will never come.
    bool completed = false;
    WrappedResultT wrapped;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = completed_.find(handle->id());
      if (it != completed_.end()) {
        wrapped = it->second;
        completed_.erase(it);
        completed = true;
      } else {
        results_[handle->id()] = promise;
      }
    }
    if (completed) {
      promise->set_value(wrapped);
    }
    return promise->get_future().share();
  }

  /// Cancel one goal. The answer arrives on the cancel-ack channel once the
  /// server has run its cancel callback, so the future resolves with what the
  /// server decided - the same thing upstream's `rcl_action` reply carries.
  std::shared_future<CancelResponse> async_cancel_goal(GoalHandleSharedPtr handle)
  {
    auto promise = std::make_shared<std::promise<CancelResponse>>();
    if (!handle) {
      promise->set_value(CancelResponse::REJECT);
      return promise->get_future().share();
    }
    const uint64_t id = handle->id();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancel_pending_[id] = [promise](CancelResponse response) {promise->set_value(response);};
    }
    detail::send_message(cancel_channel_, id, {});
    return promise->get_future().share();
  }

  std::shared_future<CancelResponse> async_cancel_all_goals()
  {
    std::vector<GoalHandleSharedPtr> handles;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto & entry : goals_) {
        if (entry.second.handle) {
          handles.push_back(entry.second.handle);
        }
      }
    }
    return cancel_batch(handles);
  }

  /// Cancel every goal this client sent at or before `time`.
  ///
  /// The cancel behaviors run this instead of `async_cancel_all_goals()`: they
  /// cancel everything older than a moment ago and must leave a goal that was
  /// sent just now alone.
  std::shared_future<CancelResponse> async_cancel_goals_before(const rclcpp::Time & time)
  {
    std::vector<GoalHandleSharedPtr> handles;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto & entry : goals_) {
        const GoalHandleSharedPtr & handle = entry.second.handle;
        if (handle && handle->sent_at() <= time) {
          handles.push_back(handle);
        }
      }
    }
    return cancel_batch(handles);
  }

  void poll()
  {
    // What the previous call held back (see the result loop below) goes out
    // first, so a caller that is spinning on `async_get_result` picks it up on
    // the very next spin.
    if (!deferred_results_.empty()) {
      auto deferred = std::move(deferred_results_);
      deferred_results_.clear();
      for (const auto & entry : deferred) {
        deliver_result(entry.first, entry.second);
      }
    }
    // Goals this call answers the ack of. See the result loop below.
    std::vector<uint64_t> acked_now;
    for (;;) {
      size_t size = 0;
      if (!ack_channel_->subscribe(ack_buffer_.data(), ack_buffer_.size(), &size, 0)) {
        break;
      }
      uint64_t id = 0;
      if (!detail::read_id(ack_buffer_, size, id)) {
        continue;
      }
      const bool accepted = size > sizeof(id) && ack_buffer_[sizeof(id)] != 0;
      Pending pending;
      bool found = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = goals_.find(id);
        if (it != goals_.end()) {
          pending = it->second;
          found = true;
        }
      }
      if (!found) {
        detail::trace_msg(
          "CLIENT", name_,
          "ack id=" + std::to_string(id) + " dropped: no such goal");
        continue;
      }
      if (pending.answered && pending.answered->exchange(true)) {
        detail::trace_msg(
          "CLIENT", name_,
          "ack id=" + std::to_string(id) + " dropped: already answered");
        continue;
      }
      detail::trace_msg(
        "CLIENT", name_,
        "recv ack id=" + std::to_string(id) + (accepted ? " ACCEPT" : " REJECT"));
      acked_now.push_back(id);
      if (!accepted) {
        pending.promise->set_value(nullptr);
        continue;
      }
      if (pending.handle) {
        pending.handle->set_status(action_msgs::msg::GoalStatus::STATUS_ACCEPTED);
      }
      pending.promise->set_value(pending.handle);
      if (pending.options.goal_response_callback) {
        pending.options.goal_response_callback(pending.handle);
      }
    }

    for (;;) {
      size_t size = 0;
      if (!cancel_ack_channel_->subscribe(
          cancel_ack_buffer_.data(), cancel_ack_buffer_.size(), &size, 0))
      {
        break;
      }
      uint64_t id = 0;
      if (!detail::read_id(cancel_ack_buffer_, size, id)) {
        continue;
      }
      const bool accepted = size > sizeof(id) && cancel_ack_buffer_[sizeof(id)] != 0;
      std::function<void(CancelResponse)> handler;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cancel_pending_.find(id);
        if (it != cancel_pending_.end()) {
          handler = it->second;
          cancel_pending_.erase(it);
        }
      }
      if (handler) {
        handler(accepted ? CancelResponse::ACCEPT : CancelResponse::REJECT);
      }
    }

    for (;;) {
      size_t size = 0;
      if (!feedback_channel_->subscribe(
          feedback_buffer_.data(), feedback_buffer_.size(), &size, 0))
      {
        break;
      }
      uint64_t id = 0;
      if (!detail::read_id(feedback_buffer_, size, id)) {
        continue;
      }
      auto feedback = std::make_shared<Feedback>();
      if (!ros2_shim::deserialize(
          feedback_buffer_.data() + sizeof(id), size - sizeof(id), *feedback))
      {
        continue;
      }
      std::function<void(GoalHandleSharedPtr, std::shared_ptr<const Feedback>)> callback;
      GoalHandleSharedPtr handle;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = goals_.find(id);
        if (it != goals_.end()) {
          callback = it->second.options.feedback_callback;
          handle = it->second.handle;
        }
      }
      if (handle) {
        handle->set_status(action_msgs::msg::GoalStatus::STATUS_EXECUTING);
      }
      if (callback) {
        callback(handle, feedback);
      }
      if (!handle) {
        detail::trace_msg(
          "CLIENT", name_,
          "feedback id=" + std::to_string(id) + " dropped: no such goal");
      }
    }

    for (;;) {
      size_t size = 0;
      if (!result_channel_->subscribe(result_buffer_.data(), result_buffer_.size(), &size, 0)) {
        break;
      }
      uint64_t id = 0;
      if (!detail::read_id(result_buffer_, size, id)) {
        continue;
      }
      WrappedResultT wrapped;
      wrapped.goal_id = detail::make_uuid(id);
      if (size > sizeof(id)) {
        wrapped.code = static_cast<ResultCode>(result_buffer_[sizeof(id)]);
        wrapped.result = std::make_shared<Result>();
        if (!ros2_shim::deserialize(
            result_buffer_.data() + sizeof(id) + 1, size - sizeof(id) - 1, *wrapped.result))
        {
          continue;
        }
      }
      // A result whose own ack is going out in this same call has to wait for
      // the next one. `nav2_behavior_tree`'s `BtActionNode` drops any result
      // that arrives while it is still waiting for the goal response - its
      // result callback returns early on `future_goal_handle_` being set, with
      // the comment "It's probably a goal result for the last goal request" -
      // and it never looks at that goal again: the tree then waits forever on a
      // goal that has already finished, which is a navigator that accepts a
      // goal, publishes feedback and never sends a `/cmd_vel`. The shared
      // memory round trip is short enough that a fast server's ack and result
      // are regularly in the ring together, so whether a goal hangs came down
      // to whether the client happened to poll between the two writes. Holding
      // the result back one call removes the race: the caller's next spin is
      // one `spin_until_future_complete` away, and by then it has the ack.
      if (std::find(acked_now.begin(), acked_now.end(), id) != acked_now.end()) {
        detail::trace_msg(
          "CLIENT", name_,
          "result id=" + std::to_string(id) + " held back: acked in this poll");
        deferred_results_.emplace_back(id, wrapped);
        continue;
      }
      deliver_result(id, wrapped);
    }
  }

  /// Hand one finished goal to whoever is waiting for it: the promise behind
  /// `async_get_result`, the caller's own result callback, and the buffer that
  /// holds a result for a caller that has not asked yet.
  void deliver_result(uint64_t id, const WrappedResultT & wrapped)
  {
    std::function<void(const WrappedResultT &)> callback;
    GoalHandleSharedPtr handle;
    std::shared_ptr<std::promise<WrappedResultT>> promise;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = goals_.find(id);
      if (it != goals_.end()) {
        callback = it->second.options.result_callback;
        handle = it->second.handle;
      }
      auto rit = results_.find(id);
      if (rit != results_.end()) {
        promise = rit->second;
        results_.erase(rit);
      } else if (it != goals_.end()) {
        // The caller may not have reached async_get_result() yet - a fast
        // in-process server answers inside the same spin - so buffer the
        // result for it. Without this the result is dropped and the caller
        // waits forever for a message that has already been consumed.
        completed_[id] = wrapped;
        while (completed_.size() > kMaxCompletedResults) {
          completed_.erase(completed_.begin());
        }
      }
      goals_.erase(id);
    }
    std::function<void(CancelResponse)> cancel_handler;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto cit = cancel_pending_.find(id);
      if (cit != cancel_pending_.end()) {
        cancel_handler = cit->second;
        cancel_pending_.erase(cit);
      }
    }
    if (cancel_handler) {
      // The goal finished before the cancel could take effect. Answering
      // keeps the caller from waiting out its whole timeout.
      cancel_handler(CancelResponse::REJECT);
    }
    if (promise) {
      promise->set_value(wrapped);
    }
    if (handle) {
      handle->set_status(status_for(wrapped.code));
    }
    if (callback) {
      callback(wrapped);
    }
    detail::trace_msg(
      "CLIENT", name_,
      "recv result id=" + std::to_string(id) + " code=" +
      std::to_string(static_cast<int>(wrapped.code)) +
      (promise ? "" : (handle ? "" : " dropped: no such goal")));

  }

private:
  /// The goal state a finished goal ends in. The wire carries rclcpp_action's
  /// own `ResultCode`; the handle reports it with the message's code, which is
  /// what the behavior tree compares against.
  static int8_t status_for(ResultCode code)
  {
    switch (code) {
      case ResultCode::SUCCEEDED: return action_msgs::msg::GoalStatus::STATUS_SUCCEEDED;
      case ResultCode::CANCELED: return action_msgs::msg::GoalStatus::STATUS_CANCELED;
      case ResultCode::ABORTED: return action_msgs::msg::GoalStatus::STATUS_ABORTED;
      case ResultCode::UNKNOWN: break;
    }
    return action_msgs::msg::GoalStatus::STATUS_UNKNOWN;
  }

  struct Pending
  {
    GoalHandleSharedPtr handle;
    SendGoalOptionsT options;
    std::shared_ptr<std::promise<GoalHandleSharedPtr>> promise;
    // Shared by the copies `poll()` makes, and set the moment the promise is
    // answered. A second answer for one goal - a stale ack the ring kept from
    // an earlier run, a server that answers twice - would call set_value on a
    // satisfied promise, and the `std::future_error` that throws would travel
    // out of `spin_once()` and take the process with it. Refusing to answer
    // twice turns that into a dropped duplicate.
    std::shared_ptr<std::atomic<bool>> answered;
  };

  rclcpp::Node * node_;
  std::string name_;
  // The group the caller asked this client to live in, or null for the node's
  // own.
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  std::shared_ptr<shm_transport::ShmChannel> goal_channel_;
  std::shared_ptr<shm_transport::ShmChannel> ack_channel_;
  std::shared_ptr<shm_transport::ShmChannel> cancel_channel_;
  std::shared_ptr<shm_transport::ShmChannel> cancel_ack_channel_;
  std::shared_ptr<shm_transport::ShmChannel> feedback_channel_;
  std::shared_ptr<shm_transport::ShmChannel> result_channel_;
  std::vector<uint8_t> ack_buffer_;
  std::vector<uint8_t> cancel_ack_buffer_;
  std::vector<uint8_t> feedback_buffer_;
  std::vector<uint8_t> result_buffer_;
  // Results that arrived before the caller asked for them, keyed by goal id.
  static constexpr std::size_t kMaxCompletedResults = 16;

  /// A cancel that asked about several goals at once. There is no single
  /// server reply to wait for, so the batch is answered when every goal in it
  /// has been answered - accepted only if none of them was turned down.
  struct CancelBatch
  {
    std::mutex mutex;
    std::shared_ptr<std::promise<CancelResponse>> promise;
    std::size_t remaining = 0;
    bool rejected = false;
  };

  std::shared_future<CancelResponse> cancel_batch(
    const std::vector<GoalHandleSharedPtr> & handles)
  {
    auto batch = std::make_shared<CancelBatch>();
    batch->promise = std::make_shared<std::promise<CancelResponse>>();
    if (handles.empty()) {
      batch->promise->set_value(CancelResponse::ACCEPT);
      return batch->promise->get_future().share();
    }
    batch->remaining = handles.size();
    std::vector<uint64_t> ids;
    ids.reserve(handles.size());
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto & handle : handles) {
        ids.push_back(handle->id());
        cancel_pending_[handle->id()] = [batch](CancelResponse response) {
            std::lock_guard<std::mutex> lock(batch->mutex);
            if (response == CancelResponse::REJECT) {
              batch->rejected = true;
            }
            if (batch->remaining > 0 && --batch->remaining == 0) {
              batch->promise->set_value(
                batch->rejected ? CancelResponse::REJECT : CancelResponse::ACCEPT);
            }
          };
      }
    }
    for (uint64_t id : ids) {
      detail::send_message(cancel_channel_, id, {});
    }
    return batch->promise->get_future().share();
  }

  std::mutex mutex_;
  std::map<uint64_t, Pending> goals_;
  std::map<uint64_t, std::function<void(CancelResponse)>> cancel_pending_;
  std::map<uint64_t, std::shared_ptr<std::promise<WrappedResultT>>> results_;
  std::map<uint64_t, WrappedResultT> completed_;
  // Results that were read in the same `poll()` call as their own ack.
  // See the result loop at the end of `poll()` for why they wait.
  std::vector<std::pair<uint64_t, WrappedResultT>> deferred_results_;
};

// ----------------------------------------------------------------- factories
template<class ActionT>
typename Server<ActionT>::SharedPtr create_server(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base,
  rclcpp::node_interfaces::NodeClockInterface::SharedPtr,
  rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr,
  rclcpp::node_interfaces::NodeWaitablesInterface::SharedPtr,
  const std::string & action_name,
  typename Server<ActionT>::GoalCallback goal_callback,
  typename Server<ActionT>::CancelCallback cancel_callback,
  typename Server<ActionT>::AcceptedCallback accepted_callback,
  rcl_action_server_options_t options = rcl_action_server_get_default_options(),
  rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
{
  auto server = std::make_shared<Server<ActionT>>(
    node_base, action_name, std::move(goal_callback), std::move(cancel_callback),
    std::move(accepted_callback), options, callback_group);
  server->register_with_node();
  return server;
}

template<class ActionT, typename NodeT>
typename Server<ActionT>::SharedPtr create_server(
  const NodeT & node, const std::string & action_name,
  typename Server<ActionT>::GoalCallback goal_callback,
  typename Server<ActionT>::CancelCallback cancel_callback,
  typename Server<ActionT>::AcceptedCallback accepted_callback,
  rcl_action_server_options_t options = rcl_action_server_get_default_options(),
  rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
{
  auto server = std::make_shared<Server<ActionT>>(
    node->get_node_base_interface(), action_name, std::move(goal_callback),
    std::move(cancel_callback), std::move(accepted_callback), options, callback_group);
  server->register_with_node();
  return server;
}

template<class ActionT, typename NodeT>
typename Client<ActionT>::SharedPtr create_client(
  const NodeT & node, const std::string & action_name,
  rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
{
  auto client = std::make_shared<Client<ActionT>>(
    node->get_node_base_interface(), action_name, callback_group);
  client->register_with_node();
  return client;
}

}  // namespace rclcpp_action

#endif  // RCLCPP_ACTION__RCLCPP_ACTION_HPP_
