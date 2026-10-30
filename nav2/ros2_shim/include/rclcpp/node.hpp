#ifndef RCLCPP__NODE_HPP_
#define RCLCPP__NODE_HPP_

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "rcl_interfaces/msg/log.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rcl_interfaces/msg/parameter_event.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/callback_group.hpp"
#include "rclcpp/client.hpp"
#include "rclcpp/clock.hpp"
#include "rclcpp/detail/parameter_event_hub.hpp"
#include "rclcpp/exceptions.hpp"
#include "rclcpp/logger.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/node_interfaces.hpp"
#include "rclcpp/node_options.hpp"
#include "rclcpp/parameter.hpp"
#include "rclcpp/parameter_service.hpp"
#include "rclcpp/publisher.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/service.hpp"
#include "rclcpp/shm_config.hpp"
#include "rclcpp/subscription.hpp"
#include "rclcpp/timer.hpp"
#include "rclcpp/utilities.hpp"
#include "rosgraph_msgs/msg/clock.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/parameter_support.hpp"
#include "ros2_shim/parameter_text.hpp"
#include "shm_transport/shm_channel.hpp"

namespace rclcpp
{

namespace detail
{

// `-r __node:=<name>` and `-r __ns:=<namespace>`, which is how nav2 hands a
// child node its identity: `nav2_costmap_2d::getChildNodeOptions()` builds the
// costmap node it adds to a server with `-r __ns:=global_costmap` and
// `-r __node:=global_costmap`, and everything the node then advertises is named
// relative to that namespace. Ignoring the rules leaves the costmap in the root
// namespace, where its `clear_entirely_global_costmap` service lands at
// `/clear_entirely_global_costmap` - and a behavior tree that asks for
// `/global_costmap/clear_entirely_global_costmap` waits for a service nobody
// advertises.
//
// The rules arrive as pairs of plain strings in `NodeOptions::arguments()`,
// since there is no rcl to parse them here. A rule may name the node it applies
// to (`global_costmap:__node:=global_costmap`, which is what nav2's
// `replaceOrAddArgument` produces for the costmaps), so the key is the last
// colon-separated field before the `:=`; the filter is not checked, because the
// node being built is the one the filter names.
inline std::string remappedArgument(
  const std::vector<std::string> & arguments, const std::string & key,
  const std::string & fallback)
{
  for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
    if (arguments[i] != "-r" && arguments[i] != "--remap") {
      continue;
    }
    const std::string & rule = arguments[i + 1];
    const std::size_t separator = rule.find(":=");
    if (separator == std::string::npos || separator == 0) {
      continue;
    }
    std::string from = rule.substr(0, separator);
    const std::size_t colon = from.rfind(':');
    if (colon != std::string::npos) {
      from = from.substr(colon + 1);
    }
    if (from == key) {
      return rule.substr(separator + 2);
    }
  }
  return fallback;
}

// One `-r <from>:=<to>` rule.
//
// `rcl` keeps these in the order they were given, expands both sides against
// the node's namespace before it compares, and stops at the first rule that
// matches - `rcl_remap.cpp`'s `rcl_remap_topic_name` walks the list and returns
// on the first hit. A rule covers every kind of name at once (topic, service,
// action), because a name is a name: the port resolves all of them through
// `Node::resolve_topic`, and that is where the rules are applied.
//
// This is the mechanism upstream's launch files use to put
// `nav2_velocity_smoother` and `nav2_collision_monitor` between the controller
// and the wheels, which is the whole reason it exists here:
//
//     -r cmd_vel:=cmd_vel_nav
//
// The two special keys `__node` and `__ns` are rules too, but they are read
// where the node's identity is built (`remappedArgument` above) instead of
// here. A rule may name the node it applies to
// (`global_costmap:__ns:=global_costmap`), so only the last colon-separated
// field is kept; the filter is not checked, because the node being built is the
// one the filter names.
struct RemapRule
{
  std::string from;
  std::string to;
};

// The callback group an entity was created with, picked out of whatever the
// caller passed after the callback.
//
// Upstream takes the group as one more argument of every creation call, and the
// ported packages spell that differently depending on which wrapper they go
// through: `nav2::interfaces::create_subscription` buries it in a
// `SubscriptionOptions`, `rclcpp::create_client` and `rclcpp::create_service`
// take it as a trailing parameter, and `nav2::LifecycleNode`'s wrappers put it
// after the QoS. All of them end up in this pack, so it is searched for the
// element that is a group and the rest are left alone - which is what keeps the
// already heavily overloaded creation signatures source compatible. A call that
// names no group at all gets `nullptr`, the node's own group, which is where
// every entity that does not ask for one belongs.
inline rclcpp::CallbackGroup::SharedPtr pick_callback_group()
{
  return nullptr;
}

template<typename FirstT, typename... RestT>
rclcpp::CallbackGroup::SharedPtr pick_callback_group(FirstT && first, RestT &&... rest)
{
  if constexpr (
    std::is_same_v<std::decay_t<FirstT>, rclcpp::CallbackGroup::SharedPtr> ||
    std::is_convertible_v<FirstT, rclcpp::CallbackGroup::SharedPtr>)
  {
    return std::forward<FirstT>(first);
  } else {
    return pick_callback_group(std::forward<RestT>(rest)...);
  }
}

inline std::vector<RemapRule> remapRules(const std::vector<std::string> & arguments)
{
  std::vector<RemapRule> rules;
  for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
    if (arguments[i] != "-r" && arguments[i] != "--remap") {
      continue;
    }
    const std::string & rule = arguments[i + 1];
    const std::size_t separator = rule.find(":=");
    if (separator == std::string::npos || separator == 0) {
      continue;
    }
    std::string from = rule.substr(0, separator);
    const std::size_t colon = from.rfind(':');
    if (colon != std::string::npos) {
      from = from.substr(colon + 1);
    }
    if (from.empty() || (from.size() > 1 && from[0] == '_' && from[1] == '_')) {
      continue;
    }
    rules.push_back(RemapRule{from, rule.substr(separator + 2)});
  }
  return rules;
}

// A ROS2 namespace is either empty (the root) or absolute: `/ns`, never `ns/`
// and never `ns`. The shim's name goes into the graph, into every topic and
// service name, and into the name a params file is matched against, so it is
// normalised once, here.
inline std::string normalisedNamespace(const std::string & value)
{
  std::string name = value;
  while (!name.empty() && name.back() == '/') {
    name.pop_back();
  }
  if (name.empty()) {
    return "";
  }
  if (name.front() != '/') {
    name.insert(name.begin(), '/');
  }
  return name;
}

}  // namespace detail

class Node : public std::enable_shared_from_this<Node>
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(Node)

  using SetParametersCallback =
    std::function<rcl_interfaces::msg::SetParametersResult(const std::vector<rclcpp::Parameter> &)>;
  using PostSetParametersCallback =
    std::function<void(const std::vector<rclcpp::Parameter> &)>;

  struct OnSetParametersCallbackHandle
  {
    using SharedPtr = std::shared_ptr<OnSetParametersCallbackHandle>;
  };

  struct PostSetParametersCallbackHandle
  {
    using SharedPtr = std::shared_ptr<PostSetParametersCallbackHandle>;
  };

  explicit Node(
    const std::string & node_name, const NodeOptions & options = NodeOptions())
  : Node(node_name, "", options)
  {
  }

  Node(
    const std::string & node_name, const std::string & node_namespace,
    const NodeOptions & options = NodeOptions())
  : name_(detail::remappedArgument(options.arguments(), "__node", node_name)),
    namespace_(detail::normalisedNamespace(
      detail::remappedArgument(options.arguments(), "__ns", node_namespace))),
    options_(options), remap_rules_(detail::remapRules(options.arguments())),
    logger_(name_), clock_(std::make_shared<rclcpp::Clock>()),
    base_interface_(std::make_shared<node_interfaces::NodeBaseInterface>()),
    parameters_interface_(std::make_shared<node_interfaces::NodeParametersInterface>()),
    topics_interface_(std::make_shared<node_interfaces::NodeTopicsInterface>()),
    timers_interface_(std::make_shared<node_interfaces::NodeTimersInterface>()),
    clock_interface_(std::make_shared<node_interfaces::NodeClockInterface>()),
    logging_interface_(std::make_shared<node_interfaces::NodeLoggingInterface>()),
    waitables_interface_(std::make_shared<node_interfaces::NodeWaitablesInterface>()),
    graph_interface_(std::make_shared<node_interfaces::NodeGraphInterface>()),
    services_interface_(std::make_shared<node_interfaces::NodeServicesInterface>())
  {
    // The ROS arguments first, then `NodeOptions::parameter_overrides()`, so a
    // value given on the command line wins over the same name in a file. That
    // the file is read *here*, per node, and not once for the process is the
    // point: nav2's servers create their costmaps as nodes of their own, built
    // from `getChildNodeOptions()` - which keeps the parent's `arguments()` and
    // drops every other field of its options - and those nodes read their
    // `local_costmap:` / `global_costmap:` block out of the same file. Expanded
    // once for the process they would keep their defaults (`global_frame: map`,
    // the default plugin list), which the server only notices at activation.
    for (const auto & parameter : parametersFromArguments(options.arguments())) {
      parameters_interface_->overrides[parameter.get_name()] = parameter.get_value();
    }
    for (const auto & parameter : options.parameter_overrides()) {
      parameters_interface_->overrides[parameter.get_name()] = parameter.get_value();
    }
    ros2_shim::graph::ProcessRegistry::instance().add_node(get_fully_qualified_name());
    parameters_interface_->node = this;
    // Every rclcpp node declares `use_sim_time`, and the ported servers read it
    // back with a bare `get_parameter("use_sim_time")` instead of declaring it
    // first - without this the planner and controller abort the moment they
    // build a costmap. The same call is what lets a `--params-file` entry turn
    // it on, which is how the stack follows the simulated robot's clock.
    const rclcpp::ParameterValue use_sim_time =
      declare_parameter("use_sim_time", rclcpp::ParameterValue(false));
    clock_->set_use_sim_time(use_sim_time.get<bool>());
    if (clock_->get_use_sim_time()) {
      follow_clock_topic();
    }
    base_interface_->node = this;
    topics_interface_->node = this;
    timers_interface_->node = this;
    clock_interface_->clock = clock_;
    clock_interface_->node = this;
    logging_interface_->logger = logger_;
    logging_interface_->node = this;
    waitables_interface_->node = this;
    graph_interface_->node = this;
    services_interface_->node = this;
    // Last, so that the six services are advertised under the name the node
    // ended up with (`-r __node:=` / `-r __ns:=` may have changed it) and a
    // client's `wait_for_service()` sees them from the moment the node
    // exists.
    if (options_.start_parameter_services()) {
      parameter_service_ = std::make_shared<ParameterService>(this);
    }
    // `/parameter_events`, the one topic a node publishes at nobody's request.
    // It is what a *listener* reads to learn that a node's parameters changed
    // without polling the six services above, and it is the only parameter
    // interface that reaches another process without that process knowing the
    // node's service names in advance. `rclcpp::ParameterEventsFilter` - which
    // the ported costmap layers include - sorts the message's three lists into
    // NEW / CHANGED / DELETED, so all three are filled when they can be: a
    // declaration lands in `new_parameters`, an applied set in
    // `changed_parameters`, and `deleted_parameters` stays empty because the
    // shim has no `undeclare_parameter` at all.
    //
    // Created last, like the services, so the topic is advertised under the name
    // the node ended up with (`-r __node:=` / `-r __ns:=` may have changed it).
    // A switch of its own rather than a second effect of `start_parameter_
    // services`: upstream keeps them separate, and
    // `nav2_ros_common::generate_internal_node()` turns both off, so a private
    // helper node opens neither the twelve service channels nor this topic.
    if (options_.start_parameter_event_publisher()) {
      // `ParametersQoS()` is upstream's QoS for this topic - keep last 1000,
      // reliable, volatile - and the same one `ParameterEventHandler` takes.
      parameter_event_publisher_ = create_publisher<rcl_interfaces::msg::ParameterEvent>(
        "/parameter_events", rclcpp::ParametersQoS());
    }
    // `/rosout`, the topic every node's log lands on. It is what a log
    // aggregator subscribes to and what `ros2 topic echo /rosout` reads: a
    // node's `RCLCPP_*` output without a console to scrape. Upstream opens it
    // on every node, so omitting it left the shim's graph one topic short of a
    // real robot's for every node in the process.
    //
    // Two switches, both upstream's: `NodeOptions::enable_rosout(false)` keeps
    // this node off the topic, and `NAV2_SHM_ROSOUT=0` keeps a whole process off
    // it.
    //
    // The publisher reaches the logging functions through the sink registry in
    // `logging.hpp`, which cannot build one itself: `Publisher` and this class
    // are declared after it, and a node includes it. This constructor is the
    // only place that owns a publisher *and* the logger a node logs through,
    // which is what makes it the place to install the sink. Created last, like
    // everything else that is advertised under the node's final name.
    if (options_.enable_rosout() && detail::rosout_enabled()) {
      rosout_publisher_ = create_publisher<rcl_interfaces::msg::Log>(
        "/rosout", rclcpp::RosoutQoS());
      detail::add_rosout_sink(
        this, name_, [this](const detail::RosoutRecord & record) {publish_rosout(record);});
    } else {
      // Registered with no sink rather than left out, so that a line logged
      // through *this* node's logger stops here instead of falling to whichever
      // node owns the first publisher: a node that is off the topic has to be
      // silent on it, not merely quiet.
      detail::add_rosout_sink(this, name_, nullptr);
    }
  }

  virtual ~Node()
  {
    // First, before the graph is touched and before anything else in this class
    // is torn down: another thread may be logging right now, and the sink it
    // would call into captures `this`. Removal takes the same lock the send
    // does, so once this returns no message can be on its way to a node that is
    // going away.
    detail::remove_rosout_sink(this);
    auto & registry = ros2_shim::graph::ProcessRegistry::instance();
    for (const auto & entry : registered_topics_) {
      registry.remove_topic(entry.first, entry.second);
    }
    registry.remove_node(get_fully_qualified_name());
  }

  const char * get_name() const {return name_.c_str();}
  const char * get_namespace() const {return namespace_.c_str();}

  std::string get_fully_qualified_name() const
  {
    return namespace_ + "/" + name_;
  }

  const rclcpp::Logger & get_logger() const {return logger_;}

  rclcpp::Clock::SharedPtr get_clock() {return clock_;}
  rclcpp::Clock::SharedPtr get_clock() const {return clock_;}

  rclcpp::Time now() const {return clock_->now();}

  const rclcpp::NodeOptions & get_node_options() const {return options_;}

  node_interfaces::NodeBaseInterface::SharedPtr get_node_base_interface()
  {
    return base_interface_;
  }

  node_interfaces::NodeParametersInterface::SharedPtr get_node_parameters_interface()
  {
    return parameters_interface_;
  }

  node_interfaces::NodeTopicsInterface::SharedPtr get_node_topics_interface()
  {
    return topics_interface_;
  }

  node_interfaces::NodeTimersInterface::SharedPtr get_node_timers_interface()
  {
    return timers_interface_;
  }

  node_interfaces::NodeClockInterface::SharedPtr get_node_clock_interface()
  {
    return clock_interface_;
  }

  node_interfaces::NodeLoggingInterface::SharedPtr get_node_logging_interface()
  {
    return logging_interface_;
  }

  node_interfaces::NodeWaitablesInterface::SharedPtr get_node_waitables_interface()
  {
    return waitables_interface_;
  }

  node_interfaces::NodeGraphInterface::SharedPtr get_node_graph_interface()
  {
    return graph_interface_;
  }

  node_interfaces::NodeServicesInterface::SharedPtr get_node_services_interface()
  {
    return services_interface_;
  }

  // Every channel-creating path funnels through here: the create_* helpers on
  // this class, rclcpp_lifecycle's publishers, rclcpp_action's goal channels
  // and rclcpp::create_publisher. Resolving the name here is what keeps a
  // relative name like nav2_amcl's `scan` on the same channel as the `/scan`
  // the bridge mirrors in.
  std::shared_ptr<shm_transport::ShmChannel> get_channel(
    const std::string & topic_name, uint32_t slot_size = 0) const
  {
    const std::string topic = resolve_topic(topic_name);
    return get_or_create_channel(
      topic, slot_size ? slot_size : shim::slot_size_for_topic(topic));
  }

  // A channel of this node's own, with its own read cursor, rather than the one
  // the node shares between everyone who asks for the same name.
  //
  // Sharing a cursor is right for a subscription - one node, one callback per
  // topic - and wrong wherever two readers in a process each have to see the
  // whole stream. Two action clients for one name are exactly that: they read
  // the same `_goal_ack`, and behind a single cursor whichever of them polls
  // first eats the answer, leaving the other to time out on a goal that was
  // accepted. Each client and server opens its own view of the channel instead,
  // which is what a separate DDS reader per client gives upstream.
  std::shared_ptr<shm_transport::ShmChannel> get_private_channel(
    const std::string & topic_name, uint32_t slot_size = 0) const
  {
    const std::string topic = resolve_topic(topic_name);
    const std::string name = shim::channel_name(topic);
    const uint32_t size = slot_size ? slot_size : shim::slot_size_for_topic(topic);
    auto channel = shm_transport::ShmChannel::open(name);
    if (!channel) {
      channel = shm_transport::ShmChannel::create(name, shim::default_slot_count(), size);
    }
    return channel;
  }

  // A node on simulated time follows `/clock`, the way rclcpp's time source
  // does: the bridge mirrors the one the ramp publishes from Ubuntu into the
  // shared graph, so `now()` and the stamps on the topics agree.
  void follow_clock_topic()
  {
    using ClockMessage = rosgraph_msgs::msg::Clock;
    clock_subscription_ = create_subscription<ClockMessage>(
      "/clock", rclcpp::QoS(1),
      [this](const ClockMessage & message) {
        clock_->set_sim_time(
          static_cast<int64_t>(message.clock.sec) * 1000000000LL +
          static_cast<int64_t>(message.clock.nanosec));
      });
  }

  void add_timer(
    typename TimerBase::SharedPtr timer,
    rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    timers_.push_back({std::move(callback_group), std::move(timer)});
  }

  void add_pollable(
    std::function<void()> poll,
    rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pollables_.push_back({std::move(callback_group), std::move(poll)});
  }

  rclcpp::CallbackGroup::SharedPtr create_callback_group(
    rclcpp::CallbackGroupType type, bool automatically_add_to_executor = true)
  {
    return std::make_shared<rclcpp::CallbackGroup>(type, automatically_add_to_executor);
  }

  // ROS 2 name resolution, limited to what the ported packages need: a name
  // that starts with '/' is already fully qualified and is used as it is,
  // anything else is relative and gets the node's namespace in front of it.
  // nav2_amcl, for example, subscribes to `scan` and `map`, which in the root
  // namespace are the `/scan` and `/map` the bridge mirrors in from Ubuntu -
  // without this they would land on unrelated channels called `scan` and
  // `map`, and the node would silently never see a laser or a map.
  //
  // `~/change_state` is the node's own private name, so it expands against the
  // node's fully qualified name instead of just the namespace. That is the name
  // rclcpp_lifecycle's services answer on, and the one
  // nav2_util::LifecycleServiceClient asks for when the lifecycle manager
  // drives `<node>/change_state`.
  std::string resolve_topic(const std::string & name) const
  {
    return apply_remap(plain_topic_name(name));
  }

  // The rules from `-r`, applied the way `rcl` applies them: a rule matches when
  // its `from` resolves to the same name, and the first match wins. Expanding
  // both sides is what makes `cmd_vel:=cmd_vel_nav` and
  // `/cmd_vel:=/cmd_vel_nav` the same rule for a node in the root namespace,
  // and what lands a relative `to` in this node's namespace like any other
  // relative name. `to` is resolved before the rules are consulted again, so a
  // chain (`/a:=/b` plus `/b:=/c`) moves one step rather than two - which is
  // also what rcl does.
  std::string apply_remap(const std::string & name) const
  {
    for (const detail::RemapRule & rule : remap_rules_) {
      if (plain_topic_name(rule.from) == name) {
        return plain_topic_name(rule.to);
      }
    }
    return name;
  }

  // The rules this node was built with, for a caller that wants to report them
  // (`ros2 param`-style tooling, and the shim's own tests).
  const std::vector<detail::RemapRule> & remap_rules() const {return remap_rules_;}

  // Everything above, before the `-r` rules: `/x` stays, `rel` gets the
  // namespace in front, and `~` expands against the node's own name.
  std::string plain_topic_name(const std::string & name) const
  {
    if (name.empty()) {
      return "/";
    }
    if (name[0] == '/') {
      return name;
    }
    if (name[0] == '~') {
      const std::string suffix =
        name.size() > 1 && name[1] == '/' ? name.substr(2) : name.substr(1);
      return plain_topic_name(name_ + "/" + suffix);
    }
    if (namespace_.empty() || namespace_ == "/") {
      return "/" + name;
    }
    if (namespace_.back() == '/') {
      return namespace_ + name;
    }
    return namespace_ + "/" + name;
  }

  // ---------------------------------------------------------------- topics
  // Advertises a topic in the shim graph so `ros2 topic list` sees it. Const
  // because nav2's factories hand the node over as `const NodeT &`; what is
  // being changed is the process-wide graph, not the node.
  void register_topic(
    const std::string & topic, const std::string & type, const std::string & role) const
  {
    ros2_shim::graph::ProcessRegistry::instance().add_topic(
      resolve_topic(topic), type, role);
    std::lock_guard<std::mutex> lock(mutex_);
    registered_topics_.emplace_back(resolve_topic(topic), role);
  }

  // The peer counts, read off the same shared graph the publishers read
  // theirs from, so a topic the bridge mirrors in counts exactly like one a
  // node in this process created. `voxel_layer` asks before it spends a
  // point-cloud-sized publish on a debugging topic nobody subscribed to, and
  // `nav2_velocity_smoother` decides from these whether it is the one driving
  // `/cmd_vel`; both want the resolved name, so a relative one is resolved the
  // same way create_publisher resolves it.
  size_t count_subscribers(const std::string & topic_name) const
  {
    return ros2_shim::graph::subscription_count(resolve_topic(topic_name));
  }

  size_t count_publishers(const std::string & topic_name) const
  {
    return ros2_shim::graph::publisher_count(resolve_topic(topic_name));
  }

  template<typename MessageT, typename Allocator = std::allocator<void>, typename... ExtraT>
  typename Publisher<MessageT, Allocator>::SharedPtr create_publisher(
    const std::string & topic_name, const rclcpp::QoS & qos, ExtraT &&...)
  {
    const std::string topic = resolve_topic(topic_name);
    auto channel = get_or_create_channel(topic, shim::slot_size_for_topic(topic));
    register_topic(topic, ros2_shim::message_type_name<MessageT>(), "pub");
    return std::make_shared<Publisher<MessageT, Allocator>>(topic, channel, qos);
  }

  template<typename MessageT, typename Allocator = std::allocator<void>, typename... ExtraT>
  typename Publisher<MessageT, Allocator>::SharedPtr create_publisher(
    const std::string & topic, size_t qos_history_depth, ExtraT &&...)
  {
    return create_publisher<MessageT, Allocator>(topic, rclcpp::QoS(qos_history_depth));
  }

  template<typename MessageT, typename CallbackT, typename... ExtraT>
  typename Subscription<MessageT>::SharedPtr create_subscription(
    const std::string & topic_name, const rclcpp::QoS & qos, CallbackT && callback,
    ExtraT &&... extra)
  {
    const std::string topic = resolve_topic(topic_name);
    const uint32_t slot_size = shim::slot_size_for_topic(topic);
    auto channel = get_or_create_channel(topic, slot_size);
    auto subscription = std::make_shared<Subscription<MessageT>>(
      topic, channel, qos, std::forward<CallbackT>(callback), slot_size);
    // Durability decides where a new subscription enters the stream. A
    // volatile one starts at the head: the shared memory ring outlives the
    // process that wrote it, so without this the subscriber would replay
    // whatever the previous writer left behind. A `transient_local` one starts
    // at the latched sample itself, which is how ROS 2 delivers the state that
    // already exists - /tf_static and latched maps are the ones that matter
    // here, and a late joiner that skipped them never learns the frames an
    // earlier process published.
    if (qos.get_durability() == rclcpp::DurabilityPolicy::TRANSIENT_LOCAL) {
      subscription->start_from_latched();
    } else {
      subscription->skip_backlog();
    }
    if (std::getenv("NAV2_SHM_SUB_DEBUG") != nullptr) {
      std::fprintf(
        stderr, "[shim_sub] create topic=%s slot_size=%u published_at_create=%llu\n",
        topic.c_str(), slot_size,
        static_cast<unsigned long long>(channel->published_count()));
      std::fflush(stderr);
    }
    register_topic(topic, ros2_shim::message_type_name<MessageT>(), "sub");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      subscriptions_.push_back(
        {detail::pick_callback_group(std::forward<ExtraT>(extra)...), subscription});
    }
    return subscription;
  }

  template<typename MessageT, typename CallbackT, typename... ExtraT>
  typename Subscription<MessageT>::SharedPtr create_subscription(
    const std::string & topic, size_t qos_history_depth, CallbackT && callback,
    ExtraT &&... extra)
  {
    return create_subscription<MessageT>(
      topic, rclcpp::QoS(qos_history_depth), std::forward<CallbackT>(callback),
      std::forward<ExtraT>(extra)...);
  }

  // ---------------------------------------------------------------- timers
  template<typename RepT, typename PeriodT, typename CallbackT, typename... ExtraT>
  typename WallTimer<std::decay_t<CallbackT>>::SharedPtr create_wall_timer(
    const std::chrono::duration<RepT, PeriodT> & period, CallbackT && callback,
    ExtraT &&... extra)
  {
    auto timer = std::make_shared<WallTimer<std::decay_t<CallbackT>>>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(period),
      std::forward<CallbackT>(callback));
    add_timer(timer, detail::pick_callback_group(std::forward<ExtraT>(extra)...));
    return timer;
  }

  template<typename CallbackT, typename... ExtraT>
  typename WallTimer<std::decay_t<CallbackT>>::SharedPtr create_wall_timer(
    const rclcpp::Duration & period, CallbackT && callback, ExtraT &&... extra)
  {
    return create_wall_timer(
      std::chrono::nanoseconds(period.nanoseconds()), std::forward<CallbackT>(callback),
      std::forward<ExtraT>(extra)...);
  }

  // -------------------------------------------------------------- services
  template<class ServiceT, typename CallbackT, typename... ExtraT>
  typename Service<ServiceT>::SharedPtr create_service(
    const std::string & service_name, CallbackT && callback, ExtraT &&... extra)
  {
    using Request = typename ServiceT::Request;
    using Response = typename ServiceT::Response;
    const std::string name = resolve_topic(service_name);
    const uint32_t slot_size = shim::slot_size_for_topic(name);
    auto request_channel = get_or_create_channel(name + "/_request", slot_size);
    auto response_channel = get_or_create_channel(name + "/_response", slot_size);
    using Callback = std::decay_t<CallbackT>;
    typename Service<ServiceT>::CallbackType cb =
      [callback = Callback(std::forward<CallbackT>(callback))](
      std::shared_ptr<rmw_request_id_t> header,
      std::shared_ptr<Request> request, std::shared_ptr<Response> response) mutable
      {
        if constexpr (std::is_invocable_v<Callback &, std::shared_ptr<rmw_request_id_t>,
          std::shared_ptr<Request>, std::shared_ptr<Response>>)
        {
          callback(header, request, response);
        } else if constexpr (std::is_invocable_v<Callback &, std::shared_ptr<Request>,
          std::shared_ptr<Response>>)
        {
          callback(request, response);
        } else {
          callback(request, response);
        }
      };
    auto service = std::make_shared<Service<ServiceT>>(
      name, request_channel, response_channel, cb, slot_size);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      services_.push_back(
        {detail::pick_callback_group(std::forward<ExtraT>(extra)...), service});
    }
    return service;
  }

  template<class ServiceT, typename... ExtraT>
  typename Client<ServiceT>::SharedPtr create_client(
    const std::string & service_name, ExtraT &&... extra)
  {
    const std::string name = resolve_topic(service_name);
    const uint32_t slot_size = shim::slot_size_for_topic(name);
    auto request_channel = get_or_create_channel(name + "/_request", slot_size);
    auto response_channel = get_or_create_channel(name + "/_response", slot_size);
    auto client = std::make_shared<Client<ServiceT>>(
      name, request_channel, response_channel, slot_size);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      clients_.push_back(
        {detail::pick_callback_group(std::forward<ExtraT>(extra)...), client});
    }
    return client;
  }

  // ------------------------------------------------------------ parameters
  bool has_parameter(const std::string & name) const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    return parameters_.count(name) > 0;
  }

  template<typename ParameterT>
  auto declare_parameter(
    const std::string & name, const ParameterT & default_value,
    const rcl_interfaces::msg::ParameterDescriptor & descriptor =
      rcl_interfaces::msg::ParameterDescriptor(),
    bool = false)
  {
    using Decayed = std::decay_t<ParameterT>;
    using ReturnT = std::conditional_t<
      std::is_same_v<Decayed, const char *> || std::is_same_v<Decayed, char *>,
      std::string, Decayed>;
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = parameters_.find(name);
    if (it == parameters_.end()) {
      const rclcpp::ParameterValue resolved =
        resolve_parameter_value<ReturnT>(name, ParameterValue(default_value));
      it = parameters_.emplace(name, Parameter(name, resolved)).first;
      record_parameter_descriptor(name, descriptor, resolved);
      publish_parameter_event({it->second}, {});
    }
    return it->second.template get_value<ReturnT>();
  }

  rclcpp::ParameterValue declare_parameter(const std::string & name)
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    if (parameters_.count(name) == 0) {
      const rclcpp::ParameterValue * override_value = parameter_override(name);
      parameters_.emplace(
        name, Parameter(name, override_value != nullptr ? *override_value : ParameterValue()));
    }
    return parameters_.at(name).get_value();
  }

  rclcpp::ParameterValue declare_parameter(
    const std::string & name, const rclcpp::ParameterValue & default_value,
    const rcl_interfaces::msg::ParameterDescriptor & descriptor =
      rcl_interfaces::msg::ParameterDescriptor(),
    bool = false)
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = parameters_.find(name);
    if (it == parameters_.end()) {
      const rclcpp::ParameterValue * override_value = parameter_override(name);
      it = parameters_.emplace(
        name, Parameter(name, override_value != nullptr ? *override_value : default_value)).first;
      record_parameter_descriptor(name, descriptor, it->second.get_value());
      publish_parameter_event({it->second}, {});
    }
    return it->second.get_value();
  }

  rclcpp::Parameter declare_parameter(
    const std::string & name, const rclcpp::ParameterType & type,
    const rcl_interfaces::msg::ParameterDescriptor & descriptor =
      rcl_interfaces::msg::ParameterDescriptor())
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    if (parameters_.count(name) == 0) {
      const rclcpp::ParameterValue * override_value = parameter_override(name);
      parameters_.emplace(
        name, Parameter(name, override_value != nullptr ? *override_value : ParameterValue()));
      // No value yet, so the type to record is the one that was declared -
      // which is what lets `set_parameters` tell "declared as a double, not
      // set yet" from "a name nobody declared".
      rcl_interfaces::msg::ParameterDescriptor declared = descriptor;
      if (declared.type == static_cast<uint8_t>(PARAMETER_NOT_SET)) {
        declared.type = static_cast<uint8_t>(type);
      }
      record_parameter_descriptor(name, declared, parameters_.at(name).get_value());
      // Announced with an unset value, which is exactly what "declared, no
      // value yet" is on this stream: `ParameterValue.type` is
      // `PARAMETER_NOT_SET` and the other nine fields carry nothing.
      publish_parameter_event({parameters_.at(name)}, {});
    }
    return parameters_.at(name);
  }

  template<typename ParameterT>
  void declare_parameters(
    const std::string & prefix, const std::map<std::string, ParameterT> & defaults)
  {
    for (const auto & entry : defaults) {
      declare_parameter(prefix + entry.first, entry.second);
    }
  }

  template<typename ParameterT>
  void declare_parameters(
    const std::string & prefix, const std::vector<std::pair<std::string, ParameterT>> & defaults)
  {
    for (const auto & entry : defaults) {
      declare_parameter(prefix + entry.first, entry.second);
    }
  }

  template<typename ParameterT>
  bool get_parameter(const std::string & name, ParameterT & value) const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = parameters_.find(name);
    if (it == parameters_.end() || it->second.get_value().is_empty()) {
      return false;
    }
    value = it->second.template get_value<ParameterT>();
    return true;
  }

  const rclcpp::Parameter & get_parameter(const std::string & name) const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = parameters_.find(name);
    if (it == parameters_.end()) {
      throw exceptions::ParameterNotDeclaredException(name);
    }
    return it->second;
  }

  bool get_parameter(const std::string & name, rclcpp::Parameter & parameter) const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = parameters_.find(name);
    if (it == parameters_.end()) {
      return false;
    }
    parameter = it->second;
    return !parameter.get_value().is_empty();
  }

  template<typename ParameterT>
  void get_parameter_or(
    const std::string & name, ParameterT & value, const ParameterT & alternative_value) const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = parameters_.find(name);
    if (it == parameters_.end() || it->second.get_value().is_empty()) {
      value = alternative_value;
      return;
    }
    value = it->second.template get_value<ParameterT>();
  }

  template<typename ParameterT>
  bool get_parameter_or(const std::string & name, ParameterT & value) const
  {
    return get_parameter(name, value);
  }

  void get_parameters(
    const std::vector<std::string> & names, std::vector<rclcpp::Parameter> & values) const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    values.clear();
    for (const auto & name : names) {
      auto it = parameters_.find(name);
      if (it != parameters_.end()) {
        values.push_back(it->second);
      }
    }
  }

  // The three questions the parameter services ask that the getters above do
  // not answer: every parameter at once, what a parameter was declared with,
  // and which of them exist at all.
  std::vector<rclcpp::Parameter> parameters_snapshot() const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    std::vector<rclcpp::Parameter> values;
    values.reserve(parameters_.size());
    for (const auto & entry : parameters_) {
      values.push_back(entry.second);
    }
    return values;
  }

  // What `describe_parameters` reports for `name`.
  //
  // A name that is not declared comes back as a `PARAMETER_NOT_SET`
  // descriptor carrying the name, rather than as an exception: the service
  // promises one descriptor per requested name, and a tool that asked about
  // five can then say which one it did not find. A parameter declared
  // without a descriptor - `declare_parameter("use_sim_time", false)` - and
  // most of the descriptors nav2 passes, which carry a description and
  // nothing else, get the type of the value that is actually there: that is
  // the answer a tool can print, and the one upstream's `NodeParameters`
  // fills in at declaration too.
  rcl_interfaces::msg::ParameterDescriptor parameter_descriptor(
    const std::string & name) const
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = parameter_descriptors_.find(name);
    rcl_interfaces::msg::ParameterDescriptor descriptor =
      it == parameter_descriptors_.end() ?
      rcl_interfaces::msg::ParameterDescriptor() : it->second;
    if (descriptor.type == static_cast<uint8_t>(PARAMETER_NOT_SET)) {
      auto value = parameters_.find(name);
      if (value != parameters_.end()) {
        descriptor.type = static_cast<uint8_t>(value->second.get_value().get_type());
      }
    }
    descriptor.name = name;
    return descriptor;
  }

  typename OnSetParametersCallbackHandle::SharedPtr add_on_set_parameters_callback(
    SetParametersCallback callback)
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    param_callbacks_.push_back(std::move(callback));
    return std::make_shared<OnSetParametersCallbackHandle>();
  }

  void remove_on_set_parameters_callback(const OnSetParametersCallbackHandle::SharedPtr &) {}
  void remove_on_set_parameters_callback(const OnSetParametersCallbackHandle *) {}

  typename PostSetParametersCallbackHandle::SharedPtr add_post_set_parameters_callback(
    PostSetParametersCallback callback)
  {
    std::lock_guard<std::mutex> lock(param_mutex_);
    post_param_callbacks_.push_back(std::move(callback));
    return std::make_shared<PostSetParametersCallbackHandle>();
  }

  void remove_post_set_parameters_callback(const PostSetParametersCallbackHandle::SharedPtr &) {}
  void remove_post_set_parameters_callback(const PostSetParametersCallbackHandle *) {}

  rcl_interfaces::msg::SetParametersResult set_parameter(const rclcpp::Parameter & parameter)
  {
    return set_parameters(std::vector<rclcpp::Parameter>{parameter})[0];
  }

  std::vector<rcl_interfaces::msg::SetParametersResult> set_parameters(
    const std::vector<rclcpp::Parameter> & parameters)
  {
    std::vector<rcl_interfaces::msg::SetParametersResult> results;
    // Which of the message's three lists each applied change belongs in has to
    // be decided against the map as it was *before* the write - a name the node
    // did not have is new, a name it already had changed - and the lock is the
    // only place that is still true. The message itself is built outside it.
    std::vector<rclcpp::Parameter> created;
    std::vector<rclcpp::Parameter> changed;
    bool all_ok = true;
    {
      std::lock_guard<std::mutex> lock(param_mutex_);
      results = validate_parameters(parameters);
      for (const auto & result : results) {
        all_ok = all_ok && result.successful;
      }
      if (all_ok) {
        for (const auto & param : parameters) {
          const bool is_new = parameters_.count(param.get_name()) == 0;
          parameters_.insert_or_assign(
            param.get_name(), Parameter(param.get_name(), param.get_value()));
          (is_new ? created : changed).push_back(parameters_.at(param.get_name()));
        }
        for (auto & callback : post_param_callbacks_) {
          callback(parameters);
        }
      }
    }
    // Announced outside the lock, and only for a set that was applied: a
    // `ParameterEventHandler` callback is user code, and Smac's reads its own
    // node's parameters. Held across the call, `param_mutex_` would deadlock on
    // that - it is a plain `std::mutex`. Delivery itself is documented in
    // `rclcpp::detail::ParameterEventHub`.
    if (all_ok) {
      detail::ParameterEventHub::instance().notify(get_fully_qualified_name(), parameters);
      publish_parameter_event(created, changed);
    }
    return results;
  }

  // ---------------------------------------------------------------- spin
  //
  // A node is routinely spun from two threads at once - nav2's
  // lifecycle_manager runs a NodeThread while the thread that called a service
  // spins waiting for the reply, and `nav2::ServiceClient` spins the node from
  // whichever thread invokes it - and upstream separates those with callback
  // groups. Here the cycle is serialized per node instead, which is what a
  // single threaded executor gives and what keeps the per-object read buffers -
  // one per subscription, service and client - safe. Recursive, so a callback
  // which spins again (a service call made from inside a callback) still works.
  // What that leaves to honour is *which* entities a spin is for.
  //
  // A group created with `automatically_add_to_executor_with_node == false`
  // belongs to nobody until an executor is handed it through
  // `add_callback_group`, and a plain spin of the node has to leave its
  // entities alone - that is what stops an internal executor from running
  // callbacks it was never given. It is load-bearing here:
  // `nav2::SimpleActionServer` puts its action server in such a group and spins
  // it on a thread of its own, and `on_cleanup` destroys that server. Were the
  // private thread's spin also to run the node's `~/change_state` service, the
  // cleanup would run on the action server's own thread, `~NodeThread` would
  // join the thread it is already on, and the process would die of
  // "Resource deadlock avoided" (see `simple_action_server.hpp`).
  void spin_once()
  {
    spin_entities(nullptr);
  }

  // Every entity the given group owns, and nothing else: what
  // `Executor::add_callback_group` drives when it was handed a group rather
  // than a node.
  void spin_once(const rclcpp::CallbackGroup::SharedPtr & callback_group)
  {
    spin_entities(callback_group.get());
  }

  // Whether a spin of this shape is responsible for the given group. A null
  // filter is the plain "spin the node" case, which owns the node's own group
  // (the entities created without one) and every group that asked to be added
  // to an executor along with the node.
  static bool spin_owns(
    const rclcpp::CallbackGroup::SharedPtr & group, const rclcpp::CallbackGroup * filter)
  {
    if (filter != nullptr) {
      return group.get() == filter;
    }
    return !group || group->automatically_add_to_executor_with_node();
  }

protected:
  // One cycle over the entities this spin owns, in the order they were created.
  // The groups are read under the same lock the creation calls take, and the
  // polls run outside it, because a subscription callback is free to create one.
  void spin_entities(const rclcpp::CallbackGroup * filter)
  {
    std::lock_guard<std::recursive_mutex> spin_lock(spin_mutex_);

    std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename SubscriptionBase::SharedPtr>>
      subscriptions;
    std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename ServiceBase::SharedPtr>>
      services;
    std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename ClientBase::SharedPtr>>
      clients;
    std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename TimerBase::SharedPtr>>
      timers;
    std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, std::function<void()>>> pollables;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      subscriptions = subscriptions_;
      services = services_;
      clients = clients_;
      timers = timers_;
      pollables = pollables_;
    }
    for (auto & entry : subscriptions) {
      if (spin_owns(entry.first, filter)) {
        entry.second->poll();
      }
    }
    for (auto & entry : services) {
      if (spin_owns(entry.first, filter)) {
        entry.second->poll();
      }
    }
    for (auto & entry : clients) {
      if (spin_owns(entry.first, filter)) {
        entry.second->poll();
      }
    }
    for (auto & entry : timers) {
      if (spin_owns(entry.first, filter)) {
        entry.second->execute_if_ready();
      }
    }
    for (auto & entry : pollables) {
      if (spin_owns(entry.first, filter)) {
        entry.second();
      }
    }
  }

  std::shared_ptr<shm_transport::ShmChannel> get_or_create_channel(
    const std::string & topic, uint32_t slot_size) const
  {
    const std::string name = shim::channel_name(topic);
    std::lock_guard<std::mutex> lock(channel_mutex_);
    auto it = channels_.find(name);
    if (it != channels_.end()) {
      return it->second;
    }
    std::shared_ptr<shm_transport::ShmChannel> channel =
      shm_transport::ShmChannel::open(name);
    if (!channel) {
      channel = shm_transport::ShmChannel::create(name, shim::default_slot_count(), slot_size);
    }
    channels_[name] = channel;
    return channel;
  }

  // The value NodeOptions carries for `name`, or nullptr when the caller of the
  // node did not override it. Overrides are collected in the constructor and
  // have to win over the default the code passes to declare_parameter(), the
  // same way `ros2 run ... --ros-args -p name:=value` does upstream.
  const rclcpp::ParameterValue * parameter_override(const std::string & name) const
  {
    auto it = parameters_interface_->overrides.find(name);
    if (it == parameters_interface_->overrides.end() || it->second.is_empty()) {
      return nullptr;
    }
    return &it->second;
  }

  // `T` is the type the caller asked for, so an override that cannot be read as
  // `T` is a configuration mistake rather than a reason to abort: the node
  // keeps the default and still starts. What it does not do any more is keep it
  // *quietly* - an override that was dropped used to read exactly like an
  // override nobody gave, and the node then reports the default it kept as if
  // the caller had never written anything. `-p map_start_pose:=[0, 0, 0]` is
  // the case that found this: three integers, where `slam_toolbox` declares a
  // double array, so the node replied "Map starting pose not specified"
  // (bridge/docs/03-validation.md, evidence 47).
  template<typename T>
  rclcpp::ParameterValue resolve_parameter_value(
    const std::string & name, const rclcpp::ParameterValue & fallback) const
  {
    const rclcpp::ParameterValue * override_value = parameter_override(name);
    if (override_value == nullptr) {
      return fallback;
    }
    try {
      return rclcpp::ParameterValue(override_value->template get_value<T>());
    } catch (const std::exception &) {
      RCLCPP_WARN(
        logger_,
        "parameter '%s' was overridden with the %s %s, which cannot be read as the %s it is "
        "declared with: keeping the declared default",
        name.c_str(),
        ros2_shim::parameter_type_name(static_cast<uint8_t>(override_value->get_type())),
        override_value->value_to_string().c_str(),
        ros2_shim::parameter_type_name(static_cast<uint8_t>(fallback.get_type())));
      return fallback;
    }
  }

  // Reads the parameter arguments out of a node's ROS arguments: files first,
  // then `-p NAME:=VALUE`, in the order they were given. `--ros-args` is a
  // separator the callers pass through, and anything else - nav2 adds
  // `-r __ns:=...` and `-r __node:=...` to a child node's arguments - is left
  // alone rather than mistaken for a parameter.
  std::vector<rclcpp::Parameter> parametersFromArguments(
    const std::vector<std::string> & arguments) const
  {
    std::vector<rclcpp::Parameter> parameters;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      const std::string & argument = arguments[i];
      if (argument == "--ros-args") {
        continue;
      }
      if (argument == "--params-file" && i + 1 < arguments.size()) {
        const std::string path = arguments[++i];
        const rclcpp::ParameterFileLoader loader = rclcpp::parameter_file_loader();
        if (loader == nullptr) {
          continue;
        }
        std::string error;
        if (!loader(path, name_, namespace_, parameters, &error)) {
          RCLCPP_ERROR(logger_, "%s", error.c_str());
        }
        continue;
      }
      if ((argument == "-p" || argument == "--param") && i + 1 < arguments.size()) {
        const std::string spec = arguments[++i];
        const std::size_t separator = spec.find(":=");
        if (separator != std::string::npos && separator != 0) {
          // The runners' `-p` and `ros2 param set` read a value with the same
          // function, so `[a, b, c]` means the same thing wherever a parameter
          // is written down: here, on a runner's command line, and over the
          // parameter services.
          std::string error;
          const rclcpp::ParameterValue value =
            ros2_shim::parseParameterText(spec.substr(separator + 2), &error);
          if (value.is_empty()) {
            RCLCPP_ERROR(logger_, "%s: %s", spec.c_str(), error.c_str());
          } else {
            parameters.emplace_back(spec.substr(0, separator), value);
          }
        }
        continue;
      }
    }
    return parameters;
  }

  // Called with `param_mutex_` held, from the `declare_parameter` overloads
  // (hence the name: it takes no lock of its own). `type` is filled in from
  // the value that was actually resolved, because a `--params-file`
  // override - or a bare `declare_parameter(name, value)`, which passes no
  // descriptor at all - leaves it unset, and a descriptor whose type is
  // unset would answer `describe_parameters` with nothing to print.
  void record_parameter_descriptor(
    const std::string & name, const rcl_interfaces::msg::ParameterDescriptor & descriptor,
    const rclcpp::ParameterValue & value)
  {
    if (parameter_descriptors_.count(name) != 0) {
      return;
    }
    rcl_interfaces::msg::ParameterDescriptor recorded = descriptor;
    recorded.name = name;
    if (recorded.type == static_cast<uint8_t>(PARAMETER_NOT_SET)) {
      recorded.type = static_cast<uint8_t>(value.get_type());
    }
    parameter_descriptors_.emplace(name, recorded);
  }

  // Fills and sends one `/parameter_events` message, or does nothing when the
  // node was built without the publisher - which is every node
  // `nav2_ros_common::generate_internal_node()` creates, and the reason this
  // check is here rather than an `if` at each of the four call sites.
  //
  // Called with `param_mutex_` held from the `declare_parameter` overloads and
  // without it from `set_parameters`; both are correct, because the payload is
  // built from the arguments and written straight into the shared-memory ring.
  // There is no callback to re-enter the node, so unlike the `ParameterEventHub`
  // announcement this is not a deadlock risk either way.
  void publish_parameter_event(
    const std::vector<rclcpp::Parameter> & new_parameters,
    const std::vector<rclcpp::Parameter> & changed_parameters)
  {
    if (!parameter_event_publisher_) {
      return;
    }
    rcl_interfaces::msg::ParameterEvent event;
    event.stamp = clock_->now().to_message();
    event.node = get_fully_qualified_name();
    event.new_parameters.reserve(new_parameters.size());
    for (const auto & parameter : new_parameters) {
      event.new_parameters.push_back(ros2_shim::to_message(parameter));
    }
    event.changed_parameters.reserve(changed_parameters.size());
    for (const auto & parameter : changed_parameters) {
      event.changed_parameters.push_back(ros2_shim::to_message(parameter));
    }
    parameter_event_publisher_->publish(event);
  }

  // Sends one `/rosout` message. Called from the logging functions - through
  // the sink the constructor installed - on whichever thread logged, so the
  // write onto the channel is serialised against itself; the publisher is a
  // single shared-memory ring, and two threads interleaving two messages in it
  // would be two messages nobody can parse.
  //
  // `record.logger_name` is the logger's name, which for every logger the shim
  // hands out is the node's name - what upstream puts in `Log::name`.
  void publish_rosout(const detail::RosoutRecord & record)
  {
    if (!rosout_publisher_) {
      return;
    }
    rcl_interfaces::msg::Log message;
    message.stamp = clock_->now().to_message();
    message.level = rosout_level(record.severity);
    message.name = record.logger_name;
    message.msg = record.text;
    message.file = record.file ? record.file : "";
    message.function = record.function ? record.function : "";
    message.line = static_cast<uint32_t>(record.line);
    std::lock_guard<std::mutex> lock(rosout_mutex_);
    rosout_publisher_->publish(message);
  }

  // The severity numbering `rcl_interfaces/msg/Log` carries, which is not the
  // order of the enum above: DEBUG is 10 and every step adds ten. A reader
  // filters on these numbers - `--level warn` in a log viewer is this one - so
  // they come from a named enum rather than being counted here.
  //
  // Not from the message type itself, which has the same five constants:
  // `windows.h` defines `ERROR` as a macro, and karto's sources reach this
  // header through `Mapper.h` with no `prelude.hpp` to undefine it, so
  // `rcl_interfaces::msg::Log::ERROR` does not compile in every translation
  // unit. `Logger::Level` is spelled out of the same numbers and collides with
  // nothing.
  static uint8_t rosout_level(detail::LogSeverity severity)
  {
    switch (severity) {
      case detail::LogSeverity::Debug: return static_cast<uint8_t>(Logger::Level::Debug);
      case detail::LogSeverity::Info: return static_cast<uint8_t>(Logger::Level::Info);
      case detail::LogSeverity::Warn: return static_cast<uint8_t>(Logger::Level::Warn);
      case detail::LogSeverity::Error: return static_cast<uint8_t>(Logger::Level::Error);
      case detail::LogSeverity::Fatal: return static_cast<uint8_t>(Logger::Level::Fatal);
    }
    return static_cast<uint8_t>(Logger::Level::Info);
  }

  std::string name_;
  std::string namespace_;
  NodeOptions options_;
  // The `-r` rules this node resolves names through, in the order they were
  // given (see `detail::remapRules`).
  std::vector<detail::RemapRule> remap_rules_;
  rclcpp::Logger logger_;
  rclcpp::Clock::SharedPtr clock_;
  typename Subscription<rosgraph_msgs::msg::Clock>::SharedPtr clock_subscription_;

  mutable std::mutex mutex_;
  mutable std::recursive_mutex spin_mutex_;
  // Each entity is held with the callback group it was created with, because
  // that is what decides which spins drive it - see `spin_entities`. The group
  // comes first so a spin can reject an entity before touching it.
  std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename SubscriptionBase::SharedPtr>>
  subscriptions_;
  std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename ServiceBase::SharedPtr>>
  services_;
  std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename ClientBase::SharedPtr>>
  clients_;
  std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, typename TimerBase::SharedPtr>>
  timers_;
  std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, std::function<void()>>> pollables_;
  mutable std::vector<std::pair<std::string, std::string>> registered_topics_;

  mutable std::mutex channel_mutex_;
  mutable std::map<std::string, std::shared_ptr<shm_transport::ShmChannel>> channels_;

  mutable std::mutex param_mutex_;
  std::map<std::string, rclcpp::Parameter> parameters_;
  // What each parameter was declared with, for `describe_parameters`. The
  // first declaration of a name wins, the way upstream keeps the descriptor
  // a parameter was given the first time it appeared.
  std::map<std::string, rcl_interfaces::msg::ParameterDescriptor> parameter_descriptors_;
  std::vector<SetParametersCallback> param_callbacks_;
  std::vector<PostSetParametersCallback> post_param_callbacks_;
  node_interfaces::NodeBaseInterface::SharedPtr base_interface_;
  node_interfaces::NodeParametersInterface::SharedPtr parameters_interface_;
  node_interfaces::NodeTopicsInterface::SharedPtr topics_interface_;
  node_interfaces::NodeTimersInterface::SharedPtr timers_interface_;
  node_interfaces::NodeClockInterface::SharedPtr clock_interface_;
  node_interfaces::NodeLoggingInterface::SharedPtr logging_interface_;
  node_interfaces::NodeWaitablesInterface::SharedPtr waitables_interface_;
  node_interfaces::NodeGraphInterface::SharedPtr graph_interface_;
  node_interfaces::NodeServicesInterface::SharedPtr services_interface_;

  // The six parameter services (`<node>/get_parameters` and friends),
  // present unless the caller turned them off with
  // `NodeOptions::start_parameter_services(false)` - which is what
  // `nav2_ros_common::generate_internal_node()` does, exactly as upstream,
  // so that nav2's private helper nodes do not each open twelve channels.
  // Started last in the constructor, once the node has a name to advertise
  // them under. Owned here; the services themselves live in `services_`
  // with every other service the node creates.
  ParameterService::SharedPtr parameter_service_;

  // The `/parameter_events` publisher, present unless the caller turned it off
  // with `NodeOptions::start_parameter_event_publisher(false)`. A switch of its
  // own, exactly as upstream has it, because the topic and the six services are
  // two different costs and nav2's internal helper nodes decline both. Empty
  // when off, which is the first thing `publish_parameter_event` tests.
  Publisher<rcl_interfaces::msg::ParameterEvent>::SharedPtr parameter_event_publisher_;

  // The `/rosout` publisher, empty unless the node took one - see the
  // constructor. It is a member rather than a local of `publish_rosout`
  // because the topic has to be advertised for as long as the node lives, and
  // `rosout_mutex_` serialises the writes onto its ring across the threads that
  // log. `publish_rosout` reads the pointer without the lock, which is sound
  // because only the constructor ever assigns it and only the sink - installed
  // by that same constructor, before any thread could have the node - reads it.
  Publisher<rcl_interfaces::msg::Log>::SharedPtr rosout_publisher_;
  mutable std::mutex rosout_mutex_;

  std::vector<rcl_interfaces::msg::SetParametersResult> validate_parameters(
    const std::vector<rclcpp::Parameter> & parameters)
  {
    for (auto & callback : param_callbacks_) {
      auto result = callback(parameters);
      if (!result.successful) {
        return {result};
      }
    }
    std::vector<rcl_interfaces::msg::SetParametersResult> results;
    for (size_t i = 0; i < parameters.size(); ++i) {
      rcl_interfaces::msg::SetParametersResult result;
      result.successful = true;
      results.push_back(result);
    }
    return results;
  }
};

// ------------------------------------------------------- parameter services
//
// The six handlers below need a complete `Node`, which is why they are here
// rather than in `rclcpp/parameter_service.hpp`: upstream keeps them in
// parameter_service.cpp, and the shim is header-only.

inline ParameterService::ParameterService(Node * node)
: node_(node)
{
  add_service<rcl_interfaces::srv::GetParameters>(
    node, "get_parameters", &ParameterService::handle_get_parameters);
  add_service<rcl_interfaces::srv::GetParameterTypes>(
    node, "get_parameter_types", &ParameterService::handle_get_parameter_types);
  add_service<rcl_interfaces::srv::SetParameters>(
    node, "set_parameters", &ParameterService::handle_set_parameters);
  add_service<rcl_interfaces::srv::SetParametersAtomically>(
    node, "set_parameters_atomically", &ParameterService::handle_set_parameters_atomically);
  add_service<rcl_interfaces::srv::DescribeParameters>(
    node, "describe_parameters", &ParameterService::handle_describe_parameters);
  add_service<rcl_interfaces::srv::ListParameters>(
    node, "list_parameters", &ParameterService::handle_list_parameters);
}

template<class ServiceT, class Handler>
void ParameterService::add_service(Node * node, const std::string & suffix, Handler handler)
{
  using Request = typename ServiceT::Request;
  using Response = typename ServiceT::Response;
  node->create_service<ServiceT>(
    node->get_fully_qualified_name() + "/" + suffix,
    [this, handler](std::shared_ptr<Request> request, std::shared_ptr<Response> response)
    {
      (this->*handler)(request, response);
    });
}

inline void ParameterService::handle_get_parameters(
  std::shared_ptr<rcl_interfaces::srv::GetParameters::Request> request,
  std::shared_ptr<rcl_interfaces::srv::GetParameters::Response> response)
{
  response->values.reserve(request->names.size());
  for (const std::string & name : request->names) {
    rclcpp::Parameter parameter;
    // `get_parameter(name, parameter)` is false for a name that is not there
    // *and* for one that was declared without a value - both of which
    // `GetParameters.srv` answers with `PARAMETER_NOT_SET`.
    if (node_->get_parameter(name, parameter)) {
      response->values.push_back(ros2_shim::to_message(parameter.get_value()));
    } else {
      response->values.push_back(rcl_interfaces::msg::ParameterValue());
    }
  }
}

inline void ParameterService::handle_get_parameter_types(
  std::shared_ptr<rcl_interfaces::srv::GetParameterTypes::Request> request,
  std::shared_ptr<rcl_interfaces::srv::GetParameterTypes::Response> response)
{
  response->types.reserve(request->names.size());
  for (const std::string & name : request->names) {
    rclcpp::Parameter parameter;
    response->types.push_back(
      node_->get_parameter(name, parameter) ?
      static_cast<uint8_t>(parameter.get_value().get_type()) :
      static_cast<uint8_t>(PARAMETER_NOT_SET));
  }
}

inline rcl_interfaces::msg::SetParametersResult ParameterService::check_settable(
  const std::string & name, const rclcpp::ParameterValue & value) const
{
  rcl_interfaces::msg::SetParametersResult result;
  if (!node_->has_parameter(name)) {
    result.successful = false;
    // The same words upstream throws with, so that a tool matching on the
    // message keeps matching.
    result.reason = exceptions::ParameterNotDeclaredException(name).what();
    return result;
  }

  const rcl_interfaces::msg::ParameterDescriptor descriptor = node_->parameter_descriptor(name);
  if (descriptor.read_only) {
    result.successful = false;
    result.reason = "parameter '" + name + "' is read-only";
    return result;
  }

  const uint8_t declared = descriptor.type;
  const uint8_t incoming = static_cast<uint8_t>(value.get_type());
  if (!descriptor.dynamic_typing && declared != static_cast<uint8_t>(PARAMETER_NOT_SET) &&
    incoming != static_cast<uint8_t>(PARAMETER_NOT_SET) && declared != incoming)
  {
    // A type change the in-process API would accept is refused here, because
    // what it leaves behind is a node whose `get_parameter<double>` throws the
    // next time it runs: the caller on the other side of a service is a
    // stranger, and a stranger changing a node's types is a mistake, not a
    // feature.
    result.successful = false;
    result.reason = "parameter '" + name + "' is a " +
      std::string(ros2_shim::parameter_type_name(declared)) + " and cannot be set from a " +
      ros2_shim::parameter_type_name(incoming);
    return result;
  }

  result.successful = true;
  return result;
}

inline void ParameterService::handle_set_parameters(
  std::shared_ptr<rcl_interfaces::srv::SetParameters::Request> request,
  std::shared_ptr<rcl_interfaces::srv::SetParameters::Response> response)
{
  for (const rcl_interfaces::msg::Parameter & message : request->parameters) {
    const rclcpp::Parameter parameter = ros2_shim::from_message(message);
    rcl_interfaces::msg::SetParametersResult result =
      check_settable(parameter.get_name(), parameter.get_value());
    if (result.successful) {
      result = node_->set_parameter(parameter);
    }
    response->results.push_back(result);
  }

  // One result per parameter, whatever the in-process API produced: an
  // on-set callback rejects the *set*, and `Node::set_parameters` reports that
  // as a single result for the whole call, while `SetParameters.srv` promises
  // one answer per name it was asked to set.
  const std::size_t wanted = request->parameters.size();
  if (response->results.size() == 1 && wanted > 1) {
    response->results.assign(wanted, response->results.front());
  }
}

inline void ParameterService::handle_set_parameters_atomically(
  std::shared_ptr<rcl_interfaces::srv::SetParametersAtomically::Request> request,
  std::shared_ptr<rcl_interfaces::srv::SetParametersAtomically::Response> response)
{
  std::vector<rclcpp::Parameter> parameters;
  parameters.reserve(request->parameters.size());
  for (const rcl_interfaces::msg::Parameter & message : request->parameters) {
    const rclcpp::Parameter parameter = ros2_shim::from_message(message);
    const rcl_interfaces::msg::SetParametersResult result =
      check_settable(parameter.get_name(), parameter.get_value());
    if (!result.successful) {
      // Nothing has been applied yet, and nothing will be: the answer to an
      // atomic call that fails part way is the one reason it failed.
      response->result = result;
      return;
    }
    parameters.push_back(parameter);
  }

  // `Node::set_parameters` is itself all-or-nothing - it applies the whole
  // vector or none of it - so the aggregate answer is simply the first
  // rejection, or success.
  for (const rcl_interfaces::msg::SetParametersResult & result : node_->set_parameters(parameters)) {
    if (!result.successful) {
      response->result = result;
      return;
    }
  }
  response->result.successful = true;
}

inline void ParameterService::handle_describe_parameters(
  std::shared_ptr<rcl_interfaces::srv::DescribeParameters::Request> request,
  std::shared_ptr<rcl_interfaces::srv::DescribeParameters::Response> response)
{
  response->descriptors.reserve(request->names.size());
  for (const std::string & name : request->names) {
    response->descriptors.push_back(node_->parameter_descriptor(name));
  }
}

inline void ParameterService::handle_list_parameters(
  std::shared_ptr<rcl_interfaces::srv::ListParameters::Request> request,
  std::shared_ptr<rcl_interfaces::srv::ListParameters::Response> response)
{
  const bool recursive = request->depth == rcl_interfaces::srv::ListParameters::Request::DEPTH_RECURSIVE;
  std::set<std::string> prefixes;
  for (const rclcpp::Parameter & parameter : node_->parameters_snapshot()) {
    const std::string & name = parameter.get_name();
    // No prefixes at all means "this node", which is one empty prefix.
    std::vector<std::string> wanted = request->prefixes;
    if (wanted.empty()) {
      wanted.push_back("");
    }
    for (const std::string & prefix : wanted) {
      if (name.compare(0, prefix.size(), prefix) != 0) {
        continue;
      }
      const std::string tail = name.substr(prefix.size());
      const std::size_t separators = static_cast<std::size_t>(
        std::count(tail.begin(), tail.end(), '.'));
      if (recursive || separators < request->depth) {
        response->result.names.push_back(name);
        break;
      }
      // Deeper than asked for: report the group it is in, trailing separator
      // included, so the answer can be used as the next request's prefix.
      std::size_t cut = 0;
      std::size_t seen = 0;
      for (std::size_t i = 0; i < tail.size(); ++i) {
        if (tail[i] == '.' && ++seen == request->depth) {
          cut = i + 1;
          break;
        }
      }
      prefixes.insert(prefix + tail.substr(0, cut));
      break;
    }
  }
  // Both answers are sorted: `parameters_snapshot()` walks a `std::map`, and
  // a `std::set` collects the prefixes, so a tool can walk the tree without
  // sorting anything itself.
  response->result.prefixes.assign(prefixes.begin(), prefixes.end());
}

namespace node_interfaces
{

using OnSetParametersCallbackHandle = rclcpp::Node::OnSetParametersCallbackHandle;
using PostSetParametersCallbackHandle = rclcpp::Node::PostSetParametersCallbackHandle;

inline bool NodeParametersInterface::has_parameter(const std::string & name) const
{
  return node && node->has_parameter(name);
}

inline rclcpp::Parameter NodeParametersInterface::get_parameter(const std::string & name) const
{
  return node->get_parameter(name);
}

inline bool NodeParametersInterface::get_parameter(
  const std::string & name, rclcpp::Parameter & parameter) const
{
  return node->get_parameter(name, parameter);
}

inline rclcpp::Parameter NodeParametersInterface::declare_parameter(
  const std::string & name, const rclcpp::ParameterValue & default_value,
  const rcl_interfaces::msg::ParameterDescriptor & descriptor)
{
  return rclcpp::Parameter(name, node->declare_parameter(name, default_value, descriptor));
}

inline rclcpp::Parameter NodeParametersInterface::declare_parameter(
  const std::string & name, rclcpp::ParameterType type,
  const rcl_interfaces::msg::ParameterDescriptor & descriptor)
{
  return node->declare_parameter(name, type, descriptor);
}

inline rcl_interfaces::msg::SetParametersResult NodeParametersInterface::set_parameter(
  const rclcpp::Parameter & parameter)
{
  return node->set_parameter(parameter);
}

inline std::vector<rcl_interfaces::msg::SetParametersResult>
NodeParametersInterface::set_parameters(const std::vector<rclcpp::Parameter> & parameters)
{
  return node->set_parameters(parameters);
}

}  // namespace node_interfaces

}  // namespace rclcpp

#endif  // RCLCPP__NODE_HPP_
