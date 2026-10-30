#ifndef ROS2_SHIM_PARAMETER_SUPPORT_HPP
#define ROS2_SHIM_PARAMETER_SUPPORT_HPP

// The `rcl_interfaces` side of parameters: both directions between
// `rclcpp::Parameter` / `rclcpp::ParameterValue` and the messages the six
// parameter services answer with, plus the spellings a tool prints.
//
// A leaf header on purpose - it includes `rclcpp/parameter.hpp` and the
// generated `rcl_interfaces` messages, never `rclcpp/node.hpp` - because both
// ends of the wire need it: the shim's own parameter services
// (`rclcpp/parameter_service.hpp`) and the bridge's `ros2` CLI, which builds a
// request and prints an answer over a shared-memory channel with no node in
// sight.

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "rcl_interfaces/msg/parameter.hpp"
#include "rcl_interfaces/msg/parameter_value.hpp"
#include "rclcpp/parameter.hpp"

namespace ros2_shim
{

// The type as `ros2 param get` and `ros2 param list --param-type` spell it.
// Not the constant names from `rcl_interfaces/msg/ParameterType.msg`
// (`PARAMETER_DOUBLE`): those are for code, this one is for somebody reading a
// terminal.
inline const char * parameter_type_name(uint8_t type)
{
  switch (type) {
    case 1: return "Boolean";
    case 2: return "Integer";
    case 3: return "Double";
    case 4: return "String";
    case 5: return "ByteArray";
    case 6: return "BoolArray";
    case 7: return "IntegerArray";
    case 8: return "DoubleArray";
    case 9: return "StringArray";
    default: return "NOT_SET";
  }
}

// `ParameterValue::Variant` is declared in `ParameterType` order, so the
// variant index *is* the type - which is why the switch below can be written
// once, next to `ParameterValue::get_type()`.
inline rcl_interfaces::msg::ParameterValue to_message(const rclcpp::ParameterValue & value)
{
  rcl_interfaces::msg::ParameterValue message;
  message.type = static_cast<uint8_t>(value.get_type());
  const rclcpp::ParameterValue::Variant & variant = value.get_variant();
  switch (variant.index()) {
    case 0: break;
    case 1: message.bool_value = std::get<bool>(variant); break;
    case 2: message.integer_value = std::get<int64_t>(variant); break;
    case 3: message.double_value = std::get<double>(variant); break;
    case 4: message.string_value = std::get<std::string>(variant); break;
    case 5: message.byte_array_value = std::get<std::vector<uint8_t>>(variant); break;
    case 6: message.bool_array_value = std::get<std::vector<bool>>(variant); break;
    case 7: message.integer_array_value = std::get<std::vector<int64_t>>(variant); break;
    case 8: message.double_array_value = std::get<std::vector<double>>(variant); break;
    case 9: message.string_array_value = std::get<std::vector<std::string>>(variant); break;
    default: break;
  }
  return message;
}

// Only the field `type` names is read, exactly as
// `rcl_interfaces/msg/ParameterValue.msg` specifies: the other nine are
// default-constructed and carry no information. `PARAMETER_NOT_SET` - a
// parameter that was declared but never given a value - comes back as an empty
// `ParameterValue`, so a caller's `is_empty()` keeps working.
inline rclcpp::ParameterValue from_message(const rcl_interfaces::msg::ParameterValue & message)
{
  switch (message.type) {
    case 1: return rclcpp::ParameterValue(message.bool_value);
    case 2: return rclcpp::ParameterValue(message.integer_value);
    case 3: return rclcpp::ParameterValue(message.double_value);
    case 4: return rclcpp::ParameterValue(message.string_value);
    case 5: return rclcpp::ParameterValue(message.byte_array_value);
    case 6: return rclcpp::ParameterValue(message.bool_array_value);
    case 7: return rclcpp::ParameterValue(message.integer_array_value);
    case 8: return rclcpp::ParameterValue(message.double_array_value);
    case 9: return rclcpp::ParameterValue(message.string_array_value);
    default: return rclcpp::ParameterValue();
  }
}

inline rcl_interfaces::msg::Parameter to_message(const rclcpp::Parameter & parameter)
{
  rcl_interfaces::msg::Parameter message;
  message.name = parameter.get_name();
  message.value = to_message(parameter.get_value());
  return message;
}

inline rclcpp::Parameter from_message(const rcl_interfaces::msg::Parameter & message)
{
  return rclcpp::Parameter(message.name, from_message(message.value));
}

// A double the shortest way that still reads as one: 15 significant digits is
// enough for every parameter nav2 declares and short of the noise a binary
// float would print at 17 (`0.1 + 0.2` prints as `0.3`, not as
// `0.30000000000000004`), and an integral value keeps a `.0` so that `20.0`
// does not come back looking like an integer.
inline std::string format_double(double value)
{
  std::ostringstream out;
  out << std::setprecision(15) << value;
  std::string text = out.str();
  // `inf` / `nan` carry their own letters; adding `.0` to them would be wrong.
  if (text.find_first_of(".eEni") == std::string::npos) {
    text += ".0";
  }
  return text;
}

inline std::string element_to_string(uint8_t value)
{
  return std::to_string(static_cast<int>(value));
}

inline std::string element_to_string(bool value)
{
  return value ? "true" : "false";
}

inline std::string element_to_string(int64_t value)
{
  return std::to_string(value);
}

inline std::string element_to_string(double value)
{
  return format_double(value);
}

// A list of strings is printed with its elements quoted - `['a', 'b']` - so
// that it cannot be mistaken for a list of bare words, and so that an element
// that happens to look like a number (`['1', '2']`) still reads as a string.
// A quote inside an element is not escaped: this is a terminal rendering, not
// something another program parses back.
inline std::string element_to_string(const std::string & value)
{
  return "'" + value + "'";
}

template<class T>
std::string format_sequence(const std::vector<T> & values)
{
  std::string text = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += element_to_string(values[i]);
  }
  return text + "]";
}

// The value as a terminal should show it: the same shape upstream's
// `ros2 param get` prints. A `NOT_SET` value has no text of its own - a caller
// says "not set" in its own words, because only it knows whether the parameter
// was undeclared or declared without a value.
inline std::string format_parameter_value(const rclcpp::ParameterValue & value)
{
  const rclcpp::ParameterValue::Variant & variant = value.get_variant();
  switch (variant.index()) {
    case 0: return "";
    case 1: return element_to_string(std::get<bool>(variant));
    case 2: return element_to_string(std::get<int64_t>(variant));
    case 3: return element_to_string(std::get<double>(variant));
    case 4: return std::get<std::string>(variant);
    case 5: return format_sequence(std::get<std::vector<uint8_t>>(variant));
    case 6: return format_sequence(std::get<std::vector<bool>>(variant));
    case 7: return format_sequence(std::get<std::vector<int64_t>>(variant));
    case 8: return format_sequence(std::get<std::vector<double>>(variant));
    case 9: return format_sequence(std::get<std::vector<std::string>>(variant));
    default: return "";
  }
}

inline std::string format_parameter_value(const rcl_interfaces::msg::ParameterValue & message)
{
  return format_parameter_value(from_message(message));
}

}  // namespace ros2_shim

#endif  // ROS2_SHIM_PARAMETER_SUPPORT_HPP
