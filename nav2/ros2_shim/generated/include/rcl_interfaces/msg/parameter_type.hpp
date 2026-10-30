#ifndef RCL_INTERFACES_MSG_PARAMETERTYPE_HPP_
#define RCL_INTERFACES_MSG_PARAMETERTYPE_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct ParameterType
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/ParameterType";
  using SharedPtr = std::shared_ptr<ParameterType>;
  using ConstSharedPtr = std::shared_ptr<const ParameterType>;
  using UniquePtr = std::unique_ptr<ParameterType>;
  static constexpr uint8_t PARAMETER_NOT_SET = 0;
  static constexpr uint8_t PARAMETER_BOOL = 1;
  static constexpr uint8_t PARAMETER_INTEGER = 2;
  static constexpr uint8_t PARAMETER_DOUBLE = 3;
  static constexpr uint8_t PARAMETER_STRING = 4;
  static constexpr uint8_t PARAMETER_BYTE_ARRAY = 5;
  static constexpr uint8_t PARAMETER_BOOL_ARRAY = 6;
  static constexpr uint8_t PARAMETER_INTEGER_ARRAY = 7;
  static constexpr uint8_t PARAMETER_DOUBLE_ARRAY = 8;
  static constexpr uint8_t PARAMETER_STRING_ARRAY = 9;

  template <class Archive>
  void visit(Archive & ar)
  {
    (void)ar;
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    (void)ar;
  }

  bool operator==(const ParameterType & other) const
  {
    (void)other;
    return true;
  }
  bool operator!=(const ParameterType & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_PARAMETERTYPE_HPP_
