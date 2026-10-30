#ifndef RCLCPP__FACTORIES_HPP_
#define RCLCPP__FACTORIES_HPP_

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "rclcpp/client.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/publisher.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/service.hpp"
#include "rclcpp/subscription.hpp"
#include "rclcpp/timer.hpp"

namespace rclcpp
{

// Everything `source` has declared, declared again on `target` with the value
// it holds right now.
//
// nav2's `BtActionServer::on_configure` declares `global_frame`,
// `robot_base_frame` and `transform_tolerance` on the navigator and then calls
// this "for BT nodes to obtain": the node it builds for the tree - the one the
// blackboard publishes as `node` - is a node of its own
// (`<parent>_<action>_rclcpp_node`), and a parameter belongs to a node name
// rather than to a process, so without this copy it has none of the three.
// `RemovePassedGoals` reads `robot_base_frame` off that node, and an empty
// frame name is what tf2 is then handed: "frame '' does not exist", once a
// tick, which is how `/navigate_through_poses` ends up aborting with an empty
// error message.
//
// The current values are copied, not the overrides, so a parameter that was
// declared with a default and then changed is copied as it is now.
template<class SourceT, class TargetT>
inline void copy_all_parameter_values(SourceT source, TargetT destination)
{
  if (!source || !destination) {
    return;
  }
  auto target_parameters = destination->get_node_parameters_interface();
  for (const rclcpp::Parameter & parameter : source->parameters_snapshot()) {
    if (parameter.get_value().is_empty()) {
      continue;
    }
    target_parameters->declare_parameter(parameter.get_name(), parameter.get_value());
  }
}

template<
  typename MessageT, typename Allocator = std::allocator<void>,
  typename PublisherT = Publisher<MessageT, Allocator>, typename NodeT>
typename PublisherT::SharedPtr create_publisher(
  const NodeT & node, const std::string & topic_name, const rclcpp::QoS & qos,
  const PublisherOptions & options = PublisherOptions())
{
  (void)options;
  auto channel = node.get_channel(topic_name);
  // nav2's publisher factory arrives here rather than at
  // Node::create_publisher, so this path has to announce the topic as well:
  // otherwise the channel exists in shared memory and messages flow, but
  // `ros2 topic list` never learns about /amcl_pose and its relatives.
  node.register_topic(topic_name, ros2_shim::message_type_name<MessageT>(), "pub");
  // The publisher carries the *resolved* name. `get_channel` and
  // `register_topic` resolve internally, but the object's own name is what
  // `Publisher::get_subscription_count()` looks up in the graph, and a
  // relative name that never gets a leading slash finds nothing there: nav2
  // then reads the count as zero, decides nobody is listening, and silently
  // drops the message - the controller computed a perfectly good 0.45 m/s
  // `/cmd_vel` and threw every one of them away because its publisher was
  // named `cmd_vel` while the graph records `/cmd_vel`.
  return std::make_shared<PublisherT>(node.resolve_topic(topic_name), channel, qos);
}

template<typename MessageT, typename CallbackT>
typename Subscription<MessageT>::SharedPtr create_subscription(
  node_interfaces::NodeParametersInterface::SharedPtr,
  node_interfaces::NodeTopicsInterface::SharedPtr topics_interface,
  const std::string & topic_name, const rclcpp::QoS & qos, CallbackT && callback,
  const SubscriptionOptions & options = SubscriptionOptions())
{
  Node * node = topics_interface ? topics_interface->node : nullptr;
  // On this path the group arrives inside the options -
  // `nav2::interfaces::create_subscription` builds them with
  // `createSubscriptionOptions`, which is where the group it was handed ends up
  // - so it has to be handed on to the node, which is what tells a spin whether
  // this subscription is its to poll.
  return node->create_subscription<MessageT>(
    topic_name, qos, std::forward<CallbackT>(callback), options.callback_group);
}

template<typename ServiceT, typename NodeT, typename CallbackT>
typename Service<ServiceT>::SharedPtr create_service(
  const NodeT & node, const std::string & service_name, CallbackT && callback,
  rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
{
  return node.template create_service<ServiceT>(
    service_name, std::forward<CallbackT>(callback), std::move(callback_group));
}

template<typename ServiceT, typename NodeT>
typename Client<ServiceT>::SharedPtr create_client(
  const NodeT & node, const std::string & service_name, bool = false)
{
  return node.template create_client<ServiceT>(service_name);
}

template<typename ServiceT, typename CallbackT>
typename Service<ServiceT>::SharedPtr create_service(
  node_interfaces::NodeBaseInterface::SharedPtr node_base,
  node_interfaces::NodeServicesInterface::SharedPtr,
  const std::string & service_name, CallbackT && callback,
  const rclcpp::QoS & = rclcpp::ServicesQoS(),
  rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
{
  return node_base->node->create_service<ServiceT>(
    service_name, std::forward<CallbackT>(callback), std::move(callback_group));
}

template<typename ServiceT>
typename Client<ServiceT>::SharedPtr create_client(
  node_interfaces::NodeBaseInterface::SharedPtr node_base,
  node_interfaces::NodeGraphInterface::SharedPtr,
  node_interfaces::NodeServicesInterface::SharedPtr,
  const std::string & service_name, const rclcpp::QoS & = rclcpp::ServicesQoS(),
  rclcpp::CallbackGroup::SharedPtr callback_group = nullptr)
{
  // The group has to survive this hop. `nav2::ServiceClient` with an internal
  // executor waits on this client from a spin that owns nothing but the group
  // the client was given, so a client left in the node's own group would never
  // have its reply read - and every lifecycle transition would time out.
  return node_base->node->create_client<ServiceT>(
    service_name, std::move(callback_group));
}

template<typename DurationRepT, typename DurationT, typename CallbackT>
typename GenericTimer<CallbackT>::SharedPtr create_timer(
  rclcpp::Clock::SharedPtr, std::chrono::duration<DurationRepT, DurationT> period,
  CallbackT callback, rclcpp::CallbackGroup::SharedPtr callback_group = nullptr,
  node_interfaces::NodeBaseInterface * = nullptr,
  node_interfaces::NodeTimersInterface * timers_interface = nullptr)
{
  auto timer = std::make_shared<GenericTimer<CallbackT>>(
    rclcpp::Duration::from_seconds(
      std::chrono::duration_cast<std::chrono::duration<double>>(period).count()),
    std::move(callback));
  if (timers_interface && timers_interface->node) {
    timers_interface->node->add_timer(timer, std::move(callback_group));
  }
  return timer;
}

}  // namespace rclcpp

#endif  // RCLCPP__FACTORIES_HPP_
