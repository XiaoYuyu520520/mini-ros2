#ifndef RCLCPP__NODE_INTERFACES_HPP_
#define RCLCPP__NODE_INTERFACES_HPP_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/callback_group.hpp"
#include "rclcpp/clock.hpp"
#include "rclcpp/context.hpp"
#include "rclcpp/logger.hpp"
#include "rclcpp/parameter.hpp"
#include "rclcpp/parameter_value.hpp"

namespace rclcpp
{

class Node;

namespace node_interfaces
{

class NodeBaseInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeBaseInterface>;

  Context::SharedPtr get_context() {return context_;}

  CallbackGroup::SharedPtr create_callback_group(
    CallbackGroupType type, bool automatically_add_to_executor = true)
  {
    return std::make_shared<CallbackGroup>(type, automatically_add_to_executor);
  }

  rclcpp::Node * node = nullptr;

private:
  // Every node shares the process-wide context: `rclcpp::shutdown()` fires the
  // pre-shutdown callbacks registered on it, and nav2 registers one per node
  // (see rclcpp/context.hpp).
  Context::SharedPtr context_ = Context::get_global();
};

class NodeParametersInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeParametersInterface>;

  bool has_parameter(const std::string & name) const;
  rclcpp::Parameter get_parameter(const std::string & name) const;
  bool get_parameter(const std::string & name, rclcpp::Parameter & parameter) const;

  // Both forward to the owning node, exactly like the getters above.
  rcl_interfaces::msg::SetParametersResult set_parameter(
    const rclcpp::Parameter & parameter);

  std::vector<rcl_interfaces::msg::SetParametersResult> set_parameters(
    const std::vector<rclcpp::Parameter> & parameters);

  rclcpp::Parameter declare_parameter(
    const std::string & name, const rclcpp::ParameterValue & default_value,
    const rcl_interfaces::msg::ParameterDescriptor & descriptor =
    rcl_interfaces::msg::ParameterDescriptor());

  rclcpp::Parameter declare_parameter(
    const std::string & name, rclcpp::ParameterType type,
    const rcl_interfaces::msg::ParameterDescriptor & descriptor =
    rcl_interfaces::msg::ParameterDescriptor());

  const std::map<std::string, rclcpp::ParameterValue> & get_parameter_overrides() const
  {
    return overrides;
  }

  rclcpp::Node * node = nullptr;
  std::map<std::string, rclcpp::ParameterValue> overrides;
};

class NodeTopicsInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeTopicsInterface>;
  rclcpp::Node * node = nullptr;
};

class NodeTimersInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeTimersInterface>;
  rclcpp::Node * node = nullptr;
};

class NodeClockInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeClockInterface>;
  rclcpp::Clock::SharedPtr get_clock() {return clock;}
  rclcpp::Clock::SharedPtr clock;
  // The node these belong to, like every other interface above. `tf2_ros::
  // MessageFilter` is handed the logging and clock interfaces rather than the
  // node (that is how nav2's `create_message_filter` calls it), and it needs
  // the node back to be polled again after it has held a message back.
  rclcpp::Node * node = nullptr;
};

class NodeLoggingInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeLoggingInterface>;
  const rclcpp::Logger & get_logger() const {return logger;}
  rclcpp::Logger logger;
  rclcpp::Node * node = nullptr;
};

class NodeWaitablesInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeWaitablesInterface>;
  rclcpp::Node * node = nullptr;
};

class NodeGraphInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeGraphInterface>;
  rclcpp::Node * node = nullptr;
};

class NodeServicesInterface
{
public:
  using SharedPtr = std::shared_ptr<NodeServicesInterface>;
  rclcpp::Node * node = nullptr;
};

}  // namespace node_interfaces
}  // namespace rclcpp

#endif  // RCLCPP__NODE_INTERFACES_HPP_
