#ifndef ROSGRAPH_MSGS_MSG_NODE_HPP_
#define ROSGRAPH_MSGS_MSG_NODE_HPP_

#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rcl_interfaces/msg/parameter_value.hpp"
#include "rosgraph_msgs/msg/action.hpp"
#include "rosgraph_msgs/msg/service.hpp"
#include "rosgraph_msgs/msg/topic.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace rosgraph_msgs
{
namespace msg
{

struct Node
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rosgraph_msgs/msg/Node";
  using SharedPtr = std::shared_ptr<Node>;
  using ConstSharedPtr = std::shared_ptr<const Node>;
  using UniquePtr = std::unique_ptr<Node>;
  using _name_type = std::string;
  std::string name;
  using _parameters_type = std::vector<::rcl_interfaces::msg::ParameterDescriptor>;
  std::vector<::rcl_interfaces::msg::ParameterDescriptor> parameters;
  using _parameter_values_type = std::vector<::rcl_interfaces::msg::ParameterValue>;
  std::vector<::rcl_interfaces::msg::ParameterValue> parameter_values;
  using _publishers_type = std::vector<::rosgraph_msgs::msg::Topic>;
  std::vector<::rosgraph_msgs::msg::Topic> publishers;
  using _subscriptions_type = std::vector<::rosgraph_msgs::msg::Topic>;
  std::vector<::rosgraph_msgs::msg::Topic> subscriptions;
  using _service_clients_type = std::vector<::rosgraph_msgs::msg::Service>;
  std::vector<::rosgraph_msgs::msg::Service> service_clients;
  using _service_servers_type = std::vector<::rosgraph_msgs::msg::Service>;
  std::vector<::rosgraph_msgs::msg::Service> service_servers;
  using _action_clients_type = std::vector<::rosgraph_msgs::msg::Action>;
  std::vector<::rosgraph_msgs::msg::Action> action_clients;
  using _action_servers_type = std::vector<::rosgraph_msgs::msg::Action>;
  std::vector<::rosgraph_msgs::msg::Action> action_servers;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(parameters);
    ar(parameter_values);
    ar(publishers);
    ar(subscriptions);
    ar(service_clients);
    ar(service_servers);
    ar(action_clients);
    ar(action_servers);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(parameters);
    ar(parameter_values);
    ar(publishers);
    ar(subscriptions);
    ar(service_clients);
    ar(service_servers);
    ar(action_clients);
    ar(action_servers);
  }

  bool operator==(const Node & other) const
  {
    return name == other.name &&
      parameters == other.parameters &&
      parameter_values == other.parameter_values &&
      publishers == other.publishers &&
      subscriptions == other.subscriptions &&
      service_clients == other.service_clients &&
      service_servers == other.service_servers &&
      action_clients == other.action_clients &&
      action_servers == other.action_servers;
  }
  bool operator!=(const Node & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rosgraph_msgs

#endif  // ROSGRAPH_MSGS_MSG_NODE_HPP_
