#ifndef RCLCPP__PARAMETER_VALUE_HPP_
#define RCLCPP__PARAMETER_VALUE_HPP_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace rclcpp
{

enum ParameterType : uint8_t
{
  PARAMETER_NOT_SET = 0,
  PARAMETER_BOOL = 1,
  PARAMETER_INTEGER = 2,
  PARAMETER_DOUBLE = 3,
  PARAMETER_STRING = 4,
  PARAMETER_BYTE_ARRAY = 5,
  PARAMETER_BOOL_ARRAY = 6,
  PARAMETER_INTEGER_ARRAY = 7,
  PARAMETER_DOUBLE_ARRAY = 8,
  PARAMETER_STRING_ARRAY = 9,
};

class ParameterValue
{
public:
  using Variant = std::variant<
    std::monostate, bool, int64_t, double, std::string,
    std::vector<uint8_t>, std::vector<bool>, std::vector<int64_t>,
    std::vector<double>, std::vector<std::string>>;

  ParameterValue() = default;

  ParameterValue(const char * value)  // NOLINT
  : value_(std::string(value))
  {
  }

  ParameterValue(const std::string & value)
  : value_(value)
  {
  }

  ParameterValue(bool value)
  : value_(value)
  {
  }

  template <
    class T,
    typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
  ParameterValue(T value)
  : value_(static_cast<int64_t>(value))
  {
  }

  template <
    class T,
    typename = std::enable_if_t<std::is_floating_point_v<T>>,
    typename = void>
  ParameterValue(T value)
  : value_(static_cast<double>(value))
  {
  }

  ParameterValue(const std::vector<uint8_t> & value)
  : value_(value)
  {
  }

  ParameterValue(const std::vector<bool> & value)
  : value_(value)
  {
  }

  ParameterValue(const std::vector<int64_t> & value)
  : value_(value)
  {
  }

  ParameterValue(const std::vector<double> & value)
  : value_(value)
  {
  }

  ParameterValue(const std::vector<std::string> & value)
  : value_(value)
  {
  }

  template <class T>
  T get_value() const
  {
    using U = std::decay_t<T>;
    if constexpr (std::is_same_v<U, bool>) {
      if (auto p = std::get_if<bool>(&value_)) {return *p;}
    } else if constexpr (std::is_integral_v<U>) {
      if (auto p = std::get_if<int64_t>(&value_)) {return static_cast<U>(*p);}
      if (auto p = std::get_if<bool>(&value_)) {return static_cast<U>(*p);}
      if (auto p = std::get_if<double>(&value_)) {return static_cast<U>(*p);}
    } else if constexpr (std::is_floating_point_v<U>) {
      if (auto p = std::get_if<double>(&value_)) {return static_cast<U>(*p);}
      if (auto p = std::get_if<int64_t>(&value_)) {return static_cast<U>(*p);}
    } else if constexpr (std::is_same_v<U, std::string>) {
      if (auto p = std::get_if<std::string>(&value_)) {return *p;}
    } else {
      if (auto p = std::get_if<U>(&value_)) {return *p;}
    }
    throw std::runtime_error("parameter value type mismatch");
  }

  template <class T>
  T get() const
  {
    return get_value<T>();
  }

  ParameterType get_type() const  {
    switch (value_.index()) {
      case 0: return PARAMETER_NOT_SET;
      case 1: return PARAMETER_BOOL;
      case 2: return PARAMETER_INTEGER;
      case 3: return PARAMETER_DOUBLE;
      case 4: return PARAMETER_STRING;
      case 5: return PARAMETER_BYTE_ARRAY;
      case 6: return PARAMETER_BOOL_ARRAY;
      case 7: return PARAMETER_INTEGER_ARRAY;
      case 8: return PARAMETER_DOUBLE_ARRAY;
      case 9: return PARAMETER_STRING_ARRAY;
      default: return PARAMETER_NOT_SET;
    }
  }

  bool is_empty() const {return value_.index() == 0;}

  const Variant & get_variant() const {return value_;}

  // A rendering for a log line, not for a terminal: the scalars keep the
  // spellings they always had (`std::to_string`, so 0.5 prints as
  // `0.500000`), and the five array types - which used to print as an empty
  // string, indistinguishable from a value nobody set - print as `[a, b]`.
  // `ros2_shim::format_parameter_value` is the one a tool prints.
  std::string value_to_string() const
  {
    if (auto p = std::get_if<bool>(&value_)) {return *p ? "true" : "false";}
    if (auto p = std::get_if<int64_t>(&value_)) {return std::to_string(*p);}
    if (auto p = std::get_if<double>(&value_)) {return std::to_string(*p);}
    if (auto p = std::get_if<std::string>(&value_)) {return *p;}
    if (auto p = std::get_if<std::vector<uint8_t>>(&value_)) {return joined(*p);}
    if (auto p = std::get_if<std::vector<bool>>(&value_)) {return joined(*p);}
    if (auto p = std::get_if<std::vector<int64_t>>(&value_)) {return joined(*p);}
    if (auto p = std::get_if<std::vector<double>>(&value_)) {return joined(*p);}
    if (auto p = std::get_if<std::vector<std::string>>(&value_)) {return joined(*p);}
    return "";
  }

  bool operator==(const ParameterValue & other) const {return value_ == other.value_;}
  bool operator!=(const ParameterValue & other) const {return value_ != other.value_;}

private:
  template<class T>
  static std::string joined(const std::vector<T> & values)
  {
    std::string text = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {text += ", ";}
      const T & element = values[i];
      if constexpr (std::is_same_v<T, bool>) {
        text += element ? "true" : "false";
      } else if constexpr (std::is_same_v<T, std::string>) {
        text += element;
      } else if constexpr (std::is_same_v<T, uint8_t>) {
        text += std::to_string(static_cast<int>(element));
      } else {
        text += std::to_string(element);
      }
    }
    return text + "]";
  }

  Variant value_;
};

}  // namespace rclcpp

#endif  // RCLCPP__PARAMETER_VALUE_HPP_
