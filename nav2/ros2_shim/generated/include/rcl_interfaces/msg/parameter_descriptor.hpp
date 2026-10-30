#ifndef RCL_INTERFACES_MSG_PARAMETERDESCRIPTOR_HPP_
#define RCL_INTERFACES_MSG_PARAMETERDESCRIPTOR_HPP_

#include "rcl_interfaces/msg/floating_point_range.hpp"
#include "rcl_interfaces/msg/integer_range.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct ParameterDescriptor
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/ParameterDescriptor";
  using SharedPtr = std::shared_ptr<ParameterDescriptor>;
  using ConstSharedPtr = std::shared_ptr<const ParameterDescriptor>;
  using UniquePtr = std::unique_ptr<ParameterDescriptor>;
  using _name_type = std::string;
  std::string name;
  using _type_type = uint8_t;
  uint8_t type = 0;
  using _description_type = std::string;
  std::string description;
  using _additional_constraints_type = std::string;
  std::string additional_constraints;
  using _read_only_type = bool;
  bool read_only = false;
  using _dynamic_typing_type = bool;
  bool dynamic_typing = false;
  using _floating_point_range_type = std::vector<::rcl_interfaces::msg::FloatingPointRange>;
  std::vector<::rcl_interfaces::msg::FloatingPointRange> floating_point_range;
  using _integer_range_type = std::vector<::rcl_interfaces::msg::IntegerRange>;
  std::vector<::rcl_interfaces::msg::IntegerRange> integer_range;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(name);
    ar(type);
    ar(description);
    ar(additional_constraints);
    ar(read_only);
    ar(dynamic_typing);
    ar(floating_point_range);
    ar(integer_range);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(name);
    ar(type);
    ar(description);
    ar(additional_constraints);
    ar(read_only);
    ar(dynamic_typing);
    ar(floating_point_range);
    ar(integer_range);
  }

  bool operator==(const ParameterDescriptor & other) const
  {
    return name == other.name &&
      type == other.type &&
      description == other.description &&
      additional_constraints == other.additional_constraints &&
      read_only == other.read_only &&
      dynamic_typing == other.dynamic_typing &&
      floating_point_range == other.floating_point_range &&
      integer_range == other.integer_range;
  }
  bool operator!=(const ParameterDescriptor & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_PARAMETERDESCRIPTOR_HPP_
