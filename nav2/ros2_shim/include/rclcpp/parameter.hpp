#ifndef RCLCPP__PARAMETER_HPP_
#define RCLCPP__PARAMETER_HPP_

#include <chrono>
#include <string>
#include <vector>

#include "rclcpp/parameter_value.hpp"

namespace rclcpp
{

class Parameter
{
public:
  Parameter() = default;

  Parameter(std::string name, ParameterValue value)
  : name_(std::move(name)), value_(std::move(value))
  {
  }

  template <class T>
  Parameter(const std::string & name, const T & value)
  : name_(name), value_(ParameterValue(value))
  {
  }

  const std::string & get_name() const {return name_;}

  rclcpp::ParameterValue get_value() const {return value_;}

  template <class T>
  T get_value() const
  {
    return value_.get_value<T>();
  }

  template <class T>
  T get() const
  {
    return value_.get_value<T>();
  }

  template <class T>
  T to_chrono() const
  {
    if (value_.get_type() == PARAMETER_DOUBLE) {
      return std::chrono::duration_cast<T>(
        std::chrono::duration<double>(value_.get_value<double>()));
    }
    if (value_.get_type() == PARAMETER_INTEGER) {
      return std::chrono::duration_cast<T>(
        std::chrono::duration<int64_t>(value_.get_value<int64_t>()));
    }
    return T{};
  }

  const ParameterValue & get_parameter_value() const {return value_;}

  ParameterType get_type() const {return value_.get_type();}

  bool as_bool() const {return value_.get_value<bool>();}
  int64_t as_int() const {return value_.get_value<int64_t>();}
  double as_double() const {return value_.get_value<double>();}
  std::string as_string() const {return value_.get_value<std::string>();}

  std::vector<uint8_t> as_byte_array() const {return value_.get_value<std::vector<uint8_t>>();}
  std::vector<bool> as_bool_array() const {return value_.get_value<std::vector<bool>>();}
  std::vector<int64_t> as_integer_array() const
  {
    return value_.get_value<std::vector<int64_t>>();
  }
  std::vector<double> as_double_array() const {return value_.get_value<std::vector<double>>();}
  std::vector<std::string> as_string_array() const
  {
    return value_.get_value<std::vector<std::string>>();
  }

  std::string value_to_string() const {return value_.value_to_string();}

  bool operator==(const Parameter & other) const
  {
    return name_ == other.name_ && value_ == other.value_;
  }

private:
  std::string name_;
  ParameterValue value_;
};

}  // namespace rclcpp

#endif  // RCLCPP__PARAMETER_HPP_
