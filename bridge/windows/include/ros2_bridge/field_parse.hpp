#ifndef ROS2_BRIDGE_FIELD_PARSE_HPP
#define ROS2_BRIDGE_FIELD_PARSE_HPP

// The `field.path=value` half of `ros2 topic pub` / `ros2 service call`:
// turning one command line assignment into one generated C++ field.
//
// Shared by type_registry.cpp (topic messages) and service_registry.cpp (the
// two halves of a service), because `ros2 service call /sim/set_paused
// std_srvs/srv/SetBool data=true` is the same operation as `ros2 topic pub
// /chatter std_msgs/msg/String data=hi`.

#include <array>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <type_traits>

namespace ros2_bridge
{

inline bool setScalar(std::string & target, const std::string & value)
{
  target = value;
  return true;
}

template<class T>
bool setScalar(T & target, const std::string & value)
{
  try {
    if constexpr (std::is_same_v<T, bool>) {
      // `ros2 topic pub <topic> <type> '{data: true}'` is YAML, and YAML 1.1
      // booleans are `true/false` and `yes/no` and `on/off` in any casing -
      // which is what real ROS2 accepts there. `1` / `0` stay because the rest
      // of this tree writes them. The one letter forms (`y`, `n`) YAML 1.1 also
      // has are deliberately left out: they are a well known YAML wart, and a
      // typo in a bool field is not worth guessing at.
      std::string lowered = value;
      for (char & character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
      }
      if (lowered == "true" || lowered == "yes" || lowered == "on" || value == "1") {
        target = true;
        return true;
      }
      if (lowered == "false" || lowered == "no" || lowered == "off" || value == "0") {
        target = false;
        return true;
      }
      return false;
    } else if constexpr (std::is_integral_v<T>) {
      target = static_cast<T>(std::stoll(value));
    } else {
      target = static_cast<T>(std::stod(value));
    }
  } catch (const std::exception &) {
    return false;
  }
  return true;
}

template<class Sequence>
bool setSequence(Sequence & target, const std::string & value)
{
  target.clear();
  std::istringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    if (item.empty()) {
      continue;
    }
    typename Sequence::value_type element{};
    if (!setScalar(element, item)) {
      return false;
    }
    target.push_back(element);
  }
  return true;
}

// Fixed size arrays (covariance matrices) have no clear()/push_back(); accept
// the same comma separated syntax but require the values to fit.
template<class T, std::size_t N>
bool setSequence(std::array<T, N> & target, const std::string & value)
{
  std::istringstream stream(value);
  std::string item;
  std::size_t index = 0;
  while (std::getline(stream, item, ',')) {
    if (item.empty()) {
      continue;
    }
    if (index >= N || !setScalar(target[index], item)) {
      return false;
    }
    ++index;
  }
  return index > 0;
}

// One assignment path, cut at the first `[index]` it carries:
//
//   poses[2].pose.position.x  ->  prefix "poses", index 2, rest "pose.position.x"
//   goals[0]                  ->  prefix "goals", index 0, rest ""
//   linear.x                  ->  indexed false, prefix "linear.x"
//
// A bracket that is not a plain index (`poses[i]`, `poses[`, `poses[0]x`) is
// left alone and comes back as an ordinary path, so it is reported as an
// unknown field rather than as a malformed index.
struct IndexedPath
{
  bool indexed = false;
  std::string prefix;
  std::size_t index = 0;
  std::string rest;
};

inline IndexedPath splitIndex(const std::string & path)
{
  IndexedPath out;
  out.prefix = path;
  const std::size_t open = path.find('[');
  if (open == std::string::npos) {
    return out;
  }
  const std::size_t close = path.find(']', open);
  if (close == std::string::npos || close == open + 1) {
    return out;
  }
  std::size_t index = 0;
  for (std::size_t i = open + 1; i < close; ++i) {
    const char c = path[i];
    if (c < '0' || c > '9') {
      return out;
    }
    index = index * 10 + static_cast<std::size_t>(c - '0');
  }
  out.indexed = true;
  out.prefix = path.substr(0, open);
  out.index = index;
  out.rest = close + 1 < path.size() && path[close + 1] == '.' ? path.substr(close + 2) :
    std::string();
  return out;
}

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_FIELD_PARSE_HPP
