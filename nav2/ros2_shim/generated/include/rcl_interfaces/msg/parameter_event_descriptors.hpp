#ifndef RCL_INTERFACES_MSG_PARAMETEREVENTDESCRIPTORS_HPP_
#define RCL_INTERFACES_MSG_PARAMETEREVENTDESCRIPTORS_HPP_

#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct ParameterEventDescriptors
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/ParameterEventDescriptors";
  using SharedPtr = std::shared_ptr<ParameterEventDescriptors>;
  using ConstSharedPtr = std::shared_ptr<const ParameterEventDescriptors>;
  using UniquePtr = std::unique_ptr<ParameterEventDescriptors>;
  using _new_parameters_type = std::vector<::rcl_interfaces::msg::ParameterDescriptor>;
  std::vector<::rcl_interfaces::msg::ParameterDescriptor> new_parameters;
  using _changed_parameters_type = std::vector<::rcl_interfaces::msg::ParameterDescriptor>;
  std::vector<::rcl_interfaces::msg::ParameterDescriptor> changed_parameters;
  using _deleted_parameters_type = std::vector<::rcl_interfaces::msg::ParameterDescriptor>;
  std::vector<::rcl_interfaces::msg::ParameterDescriptor> deleted_parameters;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(new_parameters);
    ar(changed_parameters);
    ar(deleted_parameters);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(new_parameters);
    ar(changed_parameters);
    ar(deleted_parameters);
  }

  bool operator==(const ParameterEventDescriptors & other) const
  {
    return new_parameters == other.new_parameters &&
      changed_parameters == other.changed_parameters &&
      deleted_parameters == other.deleted_parameters;
  }
  bool operator!=(const ParameterEventDescriptors & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_PARAMETEREVENTDESCRIPTORS_HPP_
