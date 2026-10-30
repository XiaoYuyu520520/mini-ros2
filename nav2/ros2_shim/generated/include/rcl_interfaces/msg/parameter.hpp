#ifndef RCL_INTERFACES_MSG_PARAMETER_HPP_
#define RCL_INTERFACES_MSG_PARAMETER_HPP_

#include "rcl_interfaces/msg/parameter_value.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct Parameter
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/Parameter";
  using SharedPtr = std::shared_ptr<Parameter>;
  using ConstSharedPtr = std::shared_ptr<const Parameter>;
  using UniquePtr = std::unique_ptr<Parameter>;
  using _name_type = std::string;
  std::string name;
  using _value_type = ::rcl_interfaces::msg::ParameterValue;
  ::rcl_interfaces::msg::ParameterValue value;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(value);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(value);
  }

  bool operator==(const Parameter & other) const
  {
    return name == other.name &&
      value == other.value;
  }
  bool operator!=(const Parameter & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_PARAMETER_HPP_
