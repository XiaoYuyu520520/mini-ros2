#ifndef COMPOSITION_INTERFACES_SRV_LOADNODE_HPP_
#define COMPOSITION_INTERFACES_SRV_LOADNODE_HPP_

#include "rcl_interfaces/msg/parameter.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace composition_interfaces
{
namespace srv
{

struct LoadNode
{
  static constexpr const char * ros2_service_type = "composition_interfaces/srv/LoadNode";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "composition_interfaces/srv/LoadNode_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _package_name_type = std::string;
      std::string package_name;
      using _plugin_name_type = std::string;
      std::string plugin_name;
      using _node_name_type = std::string;
      std::string node_name;
      using _node_namespace_type = std::string;
      std::string node_namespace;
      using _log_level_type = uint8_t;
      uint8_t log_level = 0;
      using _remap_rules_type = std::vector<std::string>;
      std::vector<std::string> remap_rules;
      using _parameters_type = std::vector<::rcl_interfaces::msg::Parameter>;
      std::vector<::rcl_interfaces::msg::Parameter> parameters;
      using _extra_arguments_type = std::vector<::rcl_interfaces::msg::Parameter>;
      std::vector<::rcl_interfaces::msg::Parameter> extra_arguments;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(package_name);
        ar(plugin_name);
        ar(node_name);
        ar(node_namespace);
        ar(log_level);
        ar(remap_rules);
        ar(parameters);
        ar(extra_arguments);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(package_name);
        ar(plugin_name);
        ar(node_name);
        ar(node_namespace);
        ar(log_level);
        ar(remap_rules);
        ar(parameters);
        ar(extra_arguments);
      }

      bool operator==(const Request & other) const
      {
        return package_name == other.package_name &&
          plugin_name == other.plugin_name &&
          node_name == other.node_name &&
          node_namespace == other.node_namespace &&
          log_level == other.log_level &&
          remap_rules == other.remap_rules &&
          parameters == other.parameters &&
          extra_arguments == other.extra_arguments;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "composition_interfaces/srv/LoadNode_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _error_message_type = std::string;
      std::string error_message;
      using _full_node_name_type = std::string;
      std::string full_node_name;
      using _unique_id_type = uint64_t;
      uint64_t unique_id = 0;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(error_message);
        ar(full_node_name);
        ar(unique_id);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(error_message);
        ar(full_node_name);
        ar(unique_id);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          error_message == other.error_message &&
          full_node_name == other.full_node_name &&
          unique_id == other.unique_id;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace composition_interfaces

#endif  // COMPOSITION_INTERFACES_SRV_LOADNODE_HPP_
