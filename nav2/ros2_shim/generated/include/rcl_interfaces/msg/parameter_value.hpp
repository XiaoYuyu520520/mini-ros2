#ifndef RCL_INTERFACES_MSG_PARAMETERVALUE_HPP_
#define RCL_INTERFACES_MSG_PARAMETERVALUE_HPP_

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace rcl_interfaces
{
namespace msg
{

struct ParameterValue
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "rcl_interfaces/msg/ParameterValue";
  using SharedPtr = std::shared_ptr<ParameterValue>;
  using ConstSharedPtr = std::shared_ptr<const ParameterValue>;
  using UniquePtr = std::unique_ptr<ParameterValue>;
  using _type_type = uint8_t;
  uint8_t type = 0;
  using _bool_value_type = bool;
  bool bool_value = false;
  using _integer_value_type = int64_t;
  int64_t integer_value = 0;
  using _double_value_type = double;
  double double_value = 0.0;
  using _string_value_type = std::string;
  std::string string_value;
  using _byte_array_value_type = std::vector<uint8_t>;
  std::vector<uint8_t> byte_array_value;
  using _bool_array_value_type = std::vector<bool>;
  std::vector<bool> bool_array_value;
  using _integer_array_value_type = std::vector<int64_t>;
  std::vector<int64_t> integer_array_value;
  using _double_array_value_type = std::vector<double>;
  std::vector<double> double_array_value;
  using _string_array_value_type = std::vector<std::string>;
  std::vector<std::string> string_array_value;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(type);
    ar(bool_value);
    ar(integer_value);
    ar(double_value);
    ar(string_value);
    ar(byte_array_value);
    ar(bool_array_value);
    ar(integer_array_value);
    ar(double_array_value);
    ar(string_array_value);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(type);
    ar(bool_value);
    ar(integer_value);
    ar(double_value);
    ar(string_value);
    ar(byte_array_value);
    ar(bool_array_value);
    ar(integer_array_value);
    ar(double_array_value);
    ar(string_array_value);
  }

  bool operator==(const ParameterValue & other) const
  {
    return type == other.type &&
      bool_value == other.bool_value &&
      integer_value == other.integer_value &&
      double_value == other.double_value &&
      string_value == other.string_value &&
      byte_array_value == other.byte_array_value &&
      bool_array_value == other.bool_array_value &&
      integer_array_value == other.integer_array_value &&
      double_array_value == other.double_array_value &&
      string_array_value == other.string_array_value;
  }
  bool operator!=(const ParameterValue & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace rcl_interfaces

#endif  // RCL_INTERFACES_MSG_PARAMETERVALUE_HPP_
