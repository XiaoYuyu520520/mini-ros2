#ifndef RCLCPP_LIFECYCLE__LIFECYCLE_NODE_HPP_
#define RCLCPP_LIFECYCLE__LIFECYCLE_NODE_HPP_

#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"
#include "lifecycle_msgs/msg/transition_description.hpp"
#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_available_states.hpp"
#include "lifecycle_msgs/srv/get_available_transitions.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp_lifecycle/lifecycle_publisher.hpp"
#include "rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp"
#include "rclcpp_lifecycle/state.hpp"

namespace rclcpp_lifecycle
{

namespace node_interfaces
{
using LifecycleNodeInterface = ::rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface;
}  // namespace node_interfaces

class LifecycleNode
  : public rclcpp::Node, public node_interfaces::LifecycleNodeInterface
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(LifecycleNode)

  explicit LifecycleNode(
    const std::string & node_name, const std::string & node_namespace = "",
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions(),
    bool enable_lifecycle_services = false)
  : rclcpp::Node(node_name, node_namespace, options),
    state_(
      lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, "unconfigured")
  {
    // `~/change_state`, `~/get_state`, ... are how rclcpp_lifecycle makes a
    // node's state machine reachable from outside the process, and they are the
    // only way the ported nav2 servers can be brought up: nav2's servers build
    // their costmaps, plugins and action servers in on_configure(), which a
    // lifecycle manager only ever asks for over these services. Upstream
    // rclcpp_lifecycle takes the same flag from Nav2's
    // `nav2::LifecycleNode`, which defaults it to true.
    if (enable_lifecycle_services) {
      create_lifecycle_services();
    }
  }

  std::shared_ptr<LifecycleNode> shared_from_this()
  {
    return std::static_pointer_cast<LifecycleNode>(
      rclcpp::Node::shared_from_this());
  }

  State get_current_state() const
  {
    std::lock_guard<std::recursive_mutex> lock(transition_mutex_);
    return state_;
  }

  LifecycleNode & operator=(const LifecycleNode &) = delete;

  State configure()
  {
    std::lock_guard<std::recursive_mutex> lock(transition_mutex_);
    return configure_locked();
  }

  State activate()
  {
    std::lock_guard<std::recursive_mutex> lock(transition_mutex_);
    return activate_locked();
  }

  State deactivate()
  {
    std::lock_guard<std::recursive_mutex> lock(transition_mutex_);
    return deactivate_locked();
  }

  State cleanup()
  {
    std::lock_guard<std::recursive_mutex> lock(transition_mutex_);
    return cleanup_locked();
  }

  State shutdown()
  {
    std::lock_guard<std::recursive_mutex> lock(transition_mutex_);
    return shutdown_locked();
  }

  // Drives one of the transitions a lifecycle manager asks for by id, and
  // reports whether the node ended up in the state that transition aims at.
  // Doing the work under the same lock the direct calls take keeps a manager
  // and an in-process `activateNode()` from transitioning the node twice at
  // once - which is what a runner that both self-activates and is managed would
  // otherwise do. The lock is recursive because nav2's own callbacks read the
  // state back while they run: `nav2::LifecycleNode::create_publisher` checks
  // `get_current_state()` to decide whether to activate the publisher it just
  // built, and that is called from inside on_configure().
  bool change_state(uint8_t transition)
  {
    std::lock_guard<std::recursive_mutex> lock(transition_mutex_);
    const uint8_t before = state_.id();
    try {
      switch (transition) {
        case lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE:
          return configure_locked().id() ==
                 lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE;
        case lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE:
          return activate_locked().id() ==
                 lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE;
        case lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE:
          return deactivate_locked().id() ==
                 lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE;
        case lifecycle_msgs::msg::Transition::TRANSITION_CLEANUP:
          return cleanup_locked().id() ==
                 lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED;
        case lifecycle_msgs::msg::Transition::TRANSITION_UNCONFIGURED_SHUTDOWN:
        case lifecycle_msgs::msg::Transition::TRANSITION_INACTIVE_SHUTDOWN:
        case lifecycle_msgs::msg::Transition::TRANSITION_ACTIVE_SHUTDOWN:
          if (state_.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) {
            deactivate_locked();
          }
          if (state_.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
            cleanup_locked();
          }
          return shutdown_locked().id() ==
                 lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED;
        default:
          std::fprintf(
            stderr, "[%s] lifecycle transition %u is not handled by the shim\n",
            get_name(), static_cast<unsigned int>(transition));
          std::fflush(stderr);
          return false;
      }
    } catch (const std::exception & error) {
      // A configure/activate callback that throws (a plugin that will not load,
      // a costmap that cannot be built) must not take the whole process down:
      // the manager sees a failed transition and reports it.
      std::fprintf(
        stderr, "[%s] lifecycle transition %u threw: %s (staying in '%s')\n",
        get_name(), static_cast<unsigned int>(transition), error.what(), state_.label().c_str());
      std::fflush(stderr);
      (void)before;
      return false;
    }
  }

  void add_managed_entity(std::shared_ptr<LifecycleManagedEntity> entity)
  {
    if (!entity) {
      return;
    }
    if (state_.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) {
      entity->on_activate();
    } else {
      entity->on_deactivate();
    }
    managed_entities_.push_back(std::move(entity));
  }

  template<typename MessageT>
  // Same two duties as `rclcpp::create_publisher` above: announce the topic in
  // the shim graph, and build the publisher around the resolved name - the
  // graph is keyed by resolved names, and a publisher holding a relative one
  // counts zero subscribers for every topic under a namespace.
  typename LifecyclePublisher<MessageT>::SharedPtr create_publisher(
    const std::string & topic_name, const rclcpp::QoS & qos = rclcpp::QoS(10))
  {
    const std::string topic = this->resolve_topic(topic_name);
    auto channel = this->get_channel(topic);
    register_topic(topic, ros2_shim::message_type_name<MessageT>(), "pub");
    auto publisher = std::make_shared<LifecyclePublisher<MessageT>>(topic, channel, qos);
    add_managed_entity(publisher);
    return publisher;
  }

  // `create_publisher<T>("topic", 10)` is what most ROS2 code writes; rclcpp's
  // QoS constructor from a depth is explicit, so the depth needs its own
  // overload here just like on rclcpp::Node.
  template<typename MessageT>
  typename LifecyclePublisher<MessageT>::SharedPtr create_publisher(
    const std::string & topic_name, size_t qos_history_depth)
  {
    return create_publisher<MessageT>(topic_name, rclcpp::QoS(qos_history_depth));
  }

protected:
  State state_;
  std::vector<std::shared_ptr<LifecycleManagedEntity>> managed_entities_;

  // Retrieved by rclcpp_lifecycle's own `get_available_transitions`; the ids
  // and labels are the ones the ROS 2 state machine defines.
  struct TransitionSpec
  {
    uint8_t id;
    const char * label;
  };

  static std::vector<TransitionSpec> transitions_from(uint8_t state_id)
  {
    switch (state_id) {
      case lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED:
        return {
          {lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE, "configure"},
          {lifecycle_msgs::msg::Transition::TRANSITION_UNCONFIGURED_SHUTDOWN, "shutdown"}};
      case lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE:
        return {
          {lifecycle_msgs::msg::Transition::TRANSITION_CLEANUP, "cleanup"},
          {lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE, "activate"},
          {lifecycle_msgs::msg::Transition::TRANSITION_INACTIVE_SHUTDOWN, "shutdown"}};
      case lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE:
        return {
          {lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE, "deactivate"},
          {lifecycle_msgs::msg::Transition::TRANSITION_ACTIVE_SHUTDOWN, "shutdown"}};
      default:
        return {};
    }
  }

  static std::vector<std::pair<uint8_t, const char *>> available_states()
  {
    return {
      {lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, "unconfigured"},
      {lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, "inactive"},
      {lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, "active"},
      {lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, "finalized"},
      {lifecycle_msgs::msg::State::TRANSITION_STATE_CONFIGURING, "configuring"},
      {lifecycle_msgs::msg::State::TRANSITION_STATE_CLEANINGUP, "cleaningup"},
      {lifecycle_msgs::msg::State::TRANSITION_STATE_SHUTTINGDOWN, "shuttingdown"},
      {lifecycle_msgs::msg::State::TRANSITION_STATE_ACTIVATING, "activating"},
      {lifecycle_msgs::msg::State::TRANSITION_STATE_DEACTIVATING, "deactivating"},
      {lifecycle_msgs::msg::State::TRANSITION_STATE_ERRORPROCESSING, "errorprocessing"}};
  }

  void create_lifecycle_services()
  {
    change_state_service_ = this->create_service<lifecycle_msgs::srv::ChangeState>(
      "~/change_state",
      [this](
        const std::shared_ptr<lifecycle_msgs::srv::ChangeState::Request> request,
        std::shared_ptr<lifecycle_msgs::srv::ChangeState::Response> response)
      {
        response->success = change_state(request->transition.id);
      });

    get_state_service_ = this->create_service<lifecycle_msgs::srv::GetState>(
      "~/get_state",
      [this](
        const std::shared_ptr<lifecycle_msgs::srv::GetState::Request>,
        std::shared_ptr<lifecycle_msgs::srv::GetState::Response> response)
      {
        const State state = get_current_state();
        response->current_state.id = state.id();
        response->current_state.label = state.label();
      });

    get_available_states_service_ = this->create_service<
      lifecycle_msgs::srv::GetAvailableStates>(
      "~/get_available_states",
      [this](
        const std::shared_ptr<lifecycle_msgs::srv::GetAvailableStates::Request>,
        std::shared_ptr<lifecycle_msgs::srv::GetAvailableStates::Response> response)
      {
        for (const auto & entry : available_states()) {
          lifecycle_msgs::msg::State state;
          state.id = entry.first;
          state.label = entry.second;
          response->available_states.push_back(state);
        }
      });

    get_available_transitions_service_ = this->create_service<
      lifecycle_msgs::srv::GetAvailableTransitions>(
      "~/get_available_transitions",
      [this](
        const std::shared_ptr<lifecycle_msgs::srv::GetAvailableTransitions::Request>,
        std::shared_ptr<lifecycle_msgs::srv::GetAvailableTransitions::Response> response)
      {
        const State state = get_current_state();
        for (const auto & spec : transitions_from(state.id())) {
          lifecycle_msgs::msg::TransitionDescription description;
          description.transition.id = spec.id;
          description.transition.label = spec.label;
          description.start_state.id = state.id();
          description.start_state.label = state.label();
          const State goal = state_after_transition(state.id(), spec.id);
          description.goal_state.id = goal.id();
          description.goal_state.label = goal.label();
          response->available_transitions.push_back(description);
        }
      });
  }

  static State state_after_transition(uint8_t state_id, uint8_t transition_id)
  {
    (void)state_id;
    switch (transition_id) {
      case lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE:
        return State(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, "inactive");
      case lifecycle_msgs::msg::Transition::TRANSITION_CLEANUP:
        return State(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, "unconfigured");
      case lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE:
        return State(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, "active");
      case lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE:
        return State(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, "inactive");
      default:
        return State(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, "finalized");
    }
  }

  // Runs one callback the way upstream's state machine does, which is what the
  // ported nodes are written against:
  //
  //   * the node is in the transition state (`configuring`, `activating`, ...)
  //     while the callback runs, so a callback that inspects the state sees the
  //     transition rather than the state it came from;
  //   * the callback is handed the state the node is leaving;
  //   * the node ends in `target`, in `failure` when the callback returns
  //     anything but SUCCESS, and back where it started when it throws.
  //
  // `transition_mutex_` has to be held; the lock is recursive so a callback may
  // read the state back.
  template<class CallbackT>
  State run_transition(
    const State & transitional, CallbackT && callback, const State & target,
    const State & failure)
  {
    const State previous = state_;
    state_ = transitional;
    bool success = false;
    try {
      success = callback(previous) == CallbackReturn::SUCCESS;
    } catch (...) {
      state_ = previous;
      throw;
    }
    state_ = success ? target : failure;
    return state_;
  }

  State configure_locked()
  {
    return run_transition(
      State(lifecycle_msgs::msg::State::TRANSITION_STATE_CONFIGURING, "configuring"),
      [this](const State & previous) {return on_configure(previous);},
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, "inactive"),
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, "unconfigured"));
  }

  State activate_locked()
  {
    if (state_.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
      return state_;
    }
    const State result = run_transition(
      State(lifecycle_msgs::msg::State::TRANSITION_STATE_ACTIVATING, "activating"),
      [this](const State & previous) {return on_activate(previous);},
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, "active"),
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, "inactive"));
    if (result.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) {
      for (auto & entity : managed_entities_) {
        entity->on_activate();
      }
    }
    return result;
  }

  State deactivate_locked()
  {
    if (state_.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) {
      return state_;
    }
    const State result = run_transition(
      State(lifecycle_msgs::msg::State::TRANSITION_STATE_DEACTIVATING, "deactivating"),
      [this](const State & previous) {return on_deactivate(previous);},
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, "inactive"),
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, "active"));
    if (result.id() == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
      for (auto & entity : managed_entities_) {
        entity->on_deactivate();
      }
    }
    return result;
  }

  State cleanup_locked()
  {
    if (state_.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
      return state_;
    }
    return run_transition(
      State(lifecycle_msgs::msg::State::TRANSITION_STATE_CLEANINGUP, "cleaningup"),
      [this](const State & previous) {return on_cleanup(previous);},
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, "unconfigured"),
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, "inactive"));
  }

  State shutdown_locked()
  {
    const State previous = state_;
    return run_transition(
      State(lifecycle_msgs::msg::State::TRANSITION_STATE_SHUTTINGDOWN, "shuttingdown"),
      [this](const State & state) {return on_shutdown(state);},
      State(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, "finalized"),
      previous);
  }

private:
  // `mutable` because the const getter has to take it too: the state is written
  // by the transition calls and read by whoever asks for it.
  mutable std::recursive_mutex transition_mutex_;
  rclcpp::Service<lifecycle_msgs::srv::ChangeState>::SharedPtr change_state_service_;
  rclcpp::Service<lifecycle_msgs::srv::GetState>::SharedPtr get_state_service_;
  rclcpp::Service<lifecycle_msgs::srv::GetAvailableStates>::SharedPtr
    get_available_states_service_;
  rclcpp::Service<lifecycle_msgs::srv::GetAvailableTransitions>::SharedPtr
    get_available_transitions_service_;
};

}  // namespace rclcpp_lifecycle

#endif  // RCLCPP_LIFECYCLE__LIFECYCLE_NODE_HPP_
