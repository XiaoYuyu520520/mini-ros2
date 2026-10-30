#ifndef RCL_INTERFACES_MSG_PARAMETEREVENT_HPP_
#define RCL_INTERFACES_MSG_PARAMETEREVENT_HPP_

#include "builtin_interfaces/msg/time.hpp"
#include "rcl_interfaces/msg/parameter.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct ParameterEvent
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/ParameterEvent";
  using SharedPtr = std::shared_ptr<ParameterEvent>;
  using ConstSharedPtr = std::shared_ptr<const ParameterEvent>;
  using UniquePtr = std::unique_ptr<ParameterEvent>;
  using _stamp_type = ::builtin_interfaces::msg::Time;
  ::builtin_interfaces::msg::Time stamp;
  using _node_type = std::string;
  std::string node;
  using _new_parameters_type = std::vector<::rcl_interfaces::msg::Parameter>;
  std::vector<::rcl_interfaces::msg::Parameter> new_parameters;
  using _changed_parameters_type = std::vector<::rcl_interfaces::msg::Parameter>;
  std::vector<::rcl_interfaces::msg::Parameter> changed_parameters;
  using _deleted_parameters_type = std::vector<::rcl_interfaces::msg::Parameter>;
  std::vector<::rcl_interfaces::msg::Parameter> deleted_parameters;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(stamp);
    ar(node);
    ar(new_parameters);
    ar(changed_parameters);
    ar(deleted_parameters);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(stamp);
    ar(node);
    ar(new_parameters);
    ar(changed_parameters);
    ar(deleted_parameters);
  }

  bool operator==(const ParameterEvent & other) const
  {
    return stamp == other.stamp &&
      node == other.node &&
      new_parameters == other.new_parameters &&
      changed_parameters == other.changed_parameters &&
      deleted_parameters == other.deleted_parameters;
  }
  bool operator!=(const ParameterEvent & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_PARAMETEREVENT_HPP_
