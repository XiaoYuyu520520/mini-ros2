#ifndef ROS2_SHIM__PARAMETER_TEXT_HPP_
#define ROS2_SHIM__PARAMETER_TEXT_HPP_

// The `NAME:=VALUE` text form ROS2 takes parameters in, as a value the shim can
// store. This is the parsing half of `-p`/`--param`, split out of the runners
// because a node reads the same syntax back out of its ROS arguments: nav2 gives
// the costmap nodes it creates a `-p use_sim_time:=...`, and the node either
// honours it or silently runs on a different clock than its parent.

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "rclcpp/parameter.hpp"

namespace ros2_shim
{

// `true`, `1` and `0.05` become a bool, an int64 or a double; everything else -
// a name, a path, a bare word - stays a string. Quoting is not visible in the
// text form, so a quoted number reads as the number it looks like.
inline rclcpp::ParameterValue parseParameterValue(const std::string & text)
{
  if (text == "true" || text == "false") {
    return rclcpp::ParameterValue(text == "true");
  }
  char * end = nullptr;
  const long long as_int = std::strtoll(text.c_str(), &end, 10);
  if (end != text.c_str() && *end == '\0') {
    return rclcpp::ParameterValue(static_cast<int64_t>(as_int));
  }
  const double as_double = std::strtod(text.c_str(), &end);
  if (end != text.c_str() && *end == '\0') {
    return rclcpp::ParameterValue(as_double);
  }
  return rclcpp::ParameterValue(text);
}

// The same rules as `parseParameterValue`, plus the one shape a parameter made
// of many values needs: a flat list, `[a, b, c]`.
//
// The elements are read with the same rules as a bare word, then the list as a
// whole takes the narrowest type they all fit: every element an integer ->
// `integer_array`, every element a number -> `double_array`, every element
// `true`/`false` -> `bool_array`, anything else -> `string_array`. A quoted
// element is a string whatever it looks like, so `['1', '2']` stays a
// `string_array`.
//
// Two shapes are refused rather than guessed at, because guessing is how a
// `string_array` of plugin names becomes an empty array nobody notices:
// `[]`, whose element type `rcl_interfaces` has no way to express, and
// `[1, 2]` for a `byte_array` parameter, which reads as an `integer_array` and
// is rejected by the node as a type change. Both are named in `error`.
//
// The list is the *whole* value: `[a, b, c]` with nothing around it but
// spaces. A `[` anywhere else is an ordinary character, so a value that merely
// contains one - `base[1]`, a path, a plugin name - stays the string it looks
// like instead of being read as a list and losing its text. That is the rule
// upstream uses, where a `-p` value goes through the YAML flow-sequence
// parser and only a value that starts with `[` is a sequence.
inline rclcpp::ParameterValue parseParameterText(
  const std::string & text, std::string * error = nullptr)
{
  if (error != nullptr) {
    error->clear();
  }
  const std::size_t open = text.find_first_not_of(" \t");
  if (open == std::string::npos) {
    return parseParameterValue(text);
  }
  const std::size_t close = text.find_last_not_of(" \t");
  if (text[open] != '[' || text[close] != ']') {
    return parseParameterValue(text);
  }

  std::vector<std::string> elements;
  bool quoted_element = false;
  std::size_t start = open + 1;
  for (std::size_t i = start; i <= close; ++i) {
    if (i != close && text[i] != ',') {
      continue;
    }
    std::string element = text.substr(start, i - start);
    start = i + 1;
    const std::size_t first = element.find_first_not_of(" \t");
    const std::size_t last = element.find_last_not_of(" \t");
    element = first == std::string::npos ? std::string() : element.substr(first, last - first + 1);
    if (element.empty()) {
      continue;
    }
    if (element.size() >= 2 && (element.front() == '"' || element.front() == '\'') &&
      element.back() == element.front())
    {
      element = element.substr(1, element.size() - 2);
      quoted_element = true;
    }
    elements.push_back(element);
  }

  if (elements.empty()) {
    if (error != nullptr) {
      *error = "an empty list has no element type (`rcl_interfaces` has no not-set array); "
        "write a list with at least one value in it";
    }
    return rclcpp::ParameterValue();
  }

  bool all_integer = !quoted_element;
  bool all_double = !quoted_element;
  bool all_bool = !quoted_element;
  std::vector<int64_t> integers;
  std::vector<double> doubles;
  std::vector<bool> bools;
  for (const std::string & element : elements) {
    const rclcpp::ParameterValue value = parseParameterValue(element);
    switch (value.get_type()) {
      case rclcpp::PARAMETER_INTEGER:
        integers.push_back(static_cast<int64_t>(value.get<int64_t>()));
        doubles.push_back(static_cast<double>(value.get<int64_t>()));
        break;
      case rclcpp::PARAMETER_DOUBLE:
        all_integer = false;
        all_bool = false;
        doubles.push_back(value.get<double>());
        break;
      case rclcpp::PARAMETER_BOOL:
        all_integer = false;
        all_double = false;
        bools.push_back(value.get<bool>());
        break;
      default:
        all_integer = false;
        all_double = false;
        all_bool = false;
        break;
    }
  }

  if (all_integer) {
    return rclcpp::ParameterValue(integers);
  }
  if (all_double) {
    return rclcpp::ParameterValue(doubles);
  }
  if (all_bool) {
    return rclcpp::ParameterValue(bools);
  }
  return rclcpp::ParameterValue(elements);
}

}  // namespace ros2_shim

#endif  // ROS2_SHIM__PARAMETER_TEXT_HPP_
