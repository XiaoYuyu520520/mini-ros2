#include "ros2_shim/params_file.hpp"

#include "rclcpp/node_options.hpp"

#include "ament_index_cpp/get_package_share_directory.hpp"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ros2_shim
{
namespace
{

// How one scalar reads. rclcpp's YAML reader decides a parameter's type from
// the token itself and so does this, which is what keeps
// `robot_model_type: nav2_amcl::DifferentialMotionModel` a string while
// `resolution: 0.05` becomes a double. `max_beams: 60` stays an integer rather
// than turning into a bool the way yaml-cpp's own bool conversion would treat
// it.
enum class ScalarKind
{
  Boolean,
  Integer,
  Double,
  String,
};

std::string lowercased(const std::string & text)
{
  std::string out;
  out.reserve(text.size());
  for (char character : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return out;
}

// Upstream's shipped config files are written for `ros2 launch`, whose
// substitution layer rewrites `$(find-pkg-share nav2_bt_navigator)` and friends
// before the file ever reaches a node. A `--params-file` read has no launch
// layer, so the tokens are expanded here. Left as text, the one in
// nav2_params.yaml -
//
//     bt_search_directories:
//       - $(find-pkg-share nav2_bt_navigator)/behavior_trees
//
// - is not a directory, and bt_navigator answers it with a filesystem error on
// activation rather than a missing-behavior-trees message.
//
// `ament_index_cpp` resolves the share directory the same way the C++ defaults
// do, so a package name means the same thing whether it comes from a file or
// from `nav2::get_package_share_directory()`. An unknown command is left alone:
// whatever it was, silently emptying it would be worse than the token showing
// up unchanged in a parameter dump.
std::string expand(const std::string & text)
{
  std::string out;
  std::size_t position = 0;
  while (true) {
    const std::size_t start = text.find("$(", position);
    if (start == std::string::npos) {
      out.append(text, position, std::string::npos);
      return out;
    }
    out.append(text, position, start - position);
    const std::size_t end = text.find(')', start + 2);
    if (end == std::string::npos) {
      out.append(text, start, std::string::npos);
      return out;
    }
    const std::string token = text.substr(start + 2, end - start - 2);
    std::istringstream stream(token);
    std::string command;
    stream >> command;
    std::string argument;
    stream >> argument;

    if (command == "find-pkg-share" && !argument.empty()) {
      out += ament_index_cpp::get_package_share_directory(argument);
    } else if (command == "find-pkg-prefix" && !argument.empty()) {
      out += ament_index_cpp::get_package_share_path(argument).parent_path().string();
    } else if (command == "env" && !argument.empty()) {
      const char * value = std::getenv(argument.c_str());
      out += value ? value : "";
    } else {
      out.append(text, start, end - start + 1);
    }
    position = end + 1;
  }
}

ScalarKind classify(const std::string & text)
{
  const std::string lower = lowercased(text);
  if (lower == "true" || lower == "false" || lower == "yes" || lower == "no" ||
    lower == "on" || lower == "off")
  {
    return ScalarKind::Boolean;
  }
  if (!text.empty()) {
    char * end = nullptr;
    std::strtoll(text.c_str(), &end, 10);
    if (end != text.c_str() && *end == '\0') {
      return ScalarKind::Integer;
    }
    std::strtod(text.c_str(), &end);
    if (end != text.c_str() && *end == '\0') {
      return ScalarKind::Double;
    }
  }
  return ScalarKind::String;
}

bool toBool(const std::string & text)
{
  const std::string lower = lowercased(text);
  return lower == "true" || lower == "yes" || lower == "on";
}

// `param:` with nothing after it declares no parameter at all, which is what
// the commented-out lines in the shipped config files rely on staying inert.
bool isNullOrEmpty(const std::string & text)
{
  return text.empty() || text == "~" || lowercased(text) == "null";
}

rclcpp::ParameterValue scalarValue(const std::string & text)
{
  switch (classify(text)) {
    case ScalarKind::Boolean:
      return rclcpp::ParameterValue(toBool(text));
    case ScalarKind::Integer:
      return rclcpp::ParameterValue(
        static_cast<int64_t>(std::strtoll(text.c_str(), nullptr, 10)));
    case ScalarKind::Double:
      return rclcpp::ParameterValue(std::strtod(text.c_str(), nullptr));
    case ScalarKind::String:
    default:
      return rclcpp::ParameterValue(text);
  }
}

// `[1, 2, 3]` is an integer array, `[0.1, 0.2]` a double array and
// `["odom", "map"]` a string array: the elements have to agree, and a single
// element that is not a number makes the whole sequence strings.
rclcpp::ParameterValue sequenceValue(const YAML::Node & node)
{
  std::vector<std::string> texts;
  bool all_bool = true;
  bool all_integer = true;
  bool all_number = true;
  for (const auto & entry : node) {
    if (!entry.IsScalar()) {
      all_bool = false;
      all_integer = false;
      all_number = false;
      break;
    }
    const std::string text = expand(entry.Scalar());
    texts.push_back(text);
    const ScalarKind kind = classify(text);
    all_bool = all_bool && kind == ScalarKind::Boolean;
    all_integer = all_integer && kind == ScalarKind::Integer;
    all_number = all_number && (kind == ScalarKind::Integer || kind == ScalarKind::Double);
  }
  if (texts.empty()) {
    return rclcpp::ParameterValue(std::vector<std::string>{});
  }
  if (all_bool) {
    std::vector<bool> values;
    values.reserve(texts.size());
    for (const auto & text : texts) {
      values.push_back(toBool(text));
    }
    return rclcpp::ParameterValue(values);
  }
  if (all_integer) {
    std::vector<int64_t> values;
    values.reserve(texts.size());
    for (const auto & text : texts) {
      values.push_back(std::strtoll(text.c_str(), nullptr, 10));
    }
    return rclcpp::ParameterValue(values);
  }
  if (all_number) {
    std::vector<double> values;
    values.reserve(texts.size());
    for (const auto & text : texts) {
      values.push_back(std::strtod(text.c_str(), nullptr));
    }
    return rclcpp::ParameterValue(values);
  }
  return rclcpp::ParameterValue(texts);
}

void flatten(
  const YAML::Node & node, const std::string & prefix,
  std::map<std::string, rclcpp::ParameterValue> & out)
{
  if (node.IsMap()) {
    for (const auto & entry : node) {
      const std::string key = entry.first.as<std::string>();
      flatten(entry.second, prefix.empty() ? key : prefix + "." + key, out);
    }
    return;
  }
  if (node.IsSequence()) {
    if (!prefix.empty()) {
      out[prefix] = sequenceValue(node);
    }
    return;
  }
  if (!node.IsScalar()) {
    return;
  }
  const std::string text = expand(node.Scalar());
  if (isNullOrEmpty(text)) {
    return;
  }
  if (!prefix.empty()) {
    out[prefix] = scalarValue(text);
  }
}

// `/**`, `/ns/**` and `**` match more than one node; everything else has to
// name this node. Keys are compared without the leading slash rclcpp treats as
// optional.
bool isWildcard(const std::string & key)
{
  return key.find("**") != std::string::npos;
}

std::string normalisedKey(const std::string & key)
{
  std::string name = key;
  while (!name.empty() && (name.back() == ':' || name.back() == ' ')) {
    name.pop_back();
  }
  if (!name.empty() && name.front() == '/') {
    name.erase(0, 1);
  }
  return name;
}

bool blockMatches(const std::string & key, const std::string & node_path)
{
  const std::string name = normalisedKey(key);
  if (name == "**" || name == node_path) {
    return true;
  }
  const std::string suffix = "/**";
  if (name.size() > suffix.size() &&
    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
  {
    const std::string prefix = name.substr(0, name.size() - suffix.size());
    return node_path.size() > prefix.size() &&
           node_path.compare(0, prefix.size(), prefix) == 0 &&
           node_path[prefix.size()] == '/';
  }
  return false;
}

void appendBlock(
  const YAML::Node & block, const std::string & node_path,
  std::vector<rclcpp::Parameter> & out)
{
  if (!block.IsMap()) {
    return;
  }
  const YAML::Node parameters = block["ros__parameters"];
  if (parameters && parameters.IsMap()) {
    std::map<std::string, rclcpp::ParameterValue> values;
    flatten(parameters, "", values);
    const std::string qualified = "/" + node_path + ".";
    for (const auto & entry : values) {
      out.emplace_back(entry.first, entry.second);
      out.emplace_back(qualified + entry.first, entry.second);
    }
    return;
  }
  // nav2_bringup's own file wraps the two costmap nodes one level deeper -
  //
  //     local_costmap:
  //       local_costmap:
  //         ros__parameters:
  //
  // - because it is written to be rewritten per node by a launch file that
  // pulls the subtree out with `root_key=<node name>`: the outer key is the one
  // that rewrite consumes and the inner one is what rclcpp then sees. Fed to a
  // node directly, the inner level is the block, so a nested key that names
  // this node again is descended into.
  const std::string name = node_path.substr(node_path.rfind('/') + 1);
  for (const auto & entry : block) {
    if (entry.first.as<std::string>() == name) {
      appendBlock(entry.second, node_path, out);
    }
  }
}

}  // namespace

void installParameterFileLoader()
{
  rclcpp::set_parameter_file_loader(&loadParamsFile);
}

bool loadParamsFile(
  const std::string & path, const std::string & node_name,
  const std::string & node_namespace, std::vector<rclcpp::Parameter> & out,
  std::string * error)
{
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception & exception) {
    if (error != nullptr) {
      *error = "cannot read '" + path + "': " + exception.what();
    }
    return false;
  }
  if (!root.IsMap()) {
    if (error != nullptr) {
      *error = "'" + path + "' does not hold a parameter map";
    }
    return false;
  }

  // Which blocks belong to this node. A node that carries a namespace is
  // matched by its fully qualified name *and* by its plain name: nav2's own
  // files write
  //
  //     global_costmap:
  //       global_costmap:
  //         ros__parameters:
  //
  // where the outer key is the namespace `getChildNodeOptions()` puts on the
  // costmap node (`-r __ns:=global_costmap`) and the inner one its name - the
  // same parameters either way, and the plain name is the only thing that
  // matches when a costmap is built without that rule.
  std::vector<std::string> node_paths{node_name};
  if (!node_namespace.empty()) {
    node_paths.push_back(normalisedKey(node_namespace + "/" + node_name));
  }
  const auto matched_path = [&node_paths](const std::string & key) -> const std::string * {
      for (const auto & candidate : node_paths) {
        if (blockMatches(key, candidate)) {
          return &candidate;
        }
      }
      return nullptr;
    };
  try {
    // Wildcards first: the node keeps one value per parameter name, so the
    // block that names this node has to be applied last to win over `/**`.
    for (const auto & entry : root) {
      const std::string key = entry.first.as<std::string>();
      if (!isWildcard(key)) {
        continue;
      }
      if (const std::string * matched = matched_path(key)) {
        appendBlock(entry.second, *matched, out);
      }
    }
    for (const auto & entry : root) {
      const std::string key = entry.first.as<std::string>();
      if (isWildcard(key)) {
        continue;
      }
      if (const std::string * matched = matched_path(key)) {
        appendBlock(entry.second, *matched, out);
      }
    }
  } catch (const YAML::Exception & exception) {
    if (error != nullptr) {
      *error = "'" + path + "': " + exception.what();
    }
    return false;
  }
  return true;
}

}  // namespace ros2_shim
