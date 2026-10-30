// `--params-file`: the config files that ship with slam_toolbox and nav2 have to
// land as the rclcpp::Parameter overrides the ported nodes declare, which is
// what makes `slam_bridge_node --params-file mapper_params_online_async.yaml`
// mean the same thing as it does under `ros2 run`.
//
// The shipped files are read from the repository (see the PARAMS_TEST_*
// definitions in CMakeLists.txt); the wildcard, namespace and array cases have
// no upstream file to borrow, so they are written next to the test binary.

#include "ros2_shim/params_file.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

// The last entry wins: a parameter file may set the same name in a `/**` block
// and again in the block that names the node, and `rclcpp::Node` keeps the one
// it reads last, so the list has to be searched from the back to see the value
// the node would actually end up with.
const rclcpp::ParameterValue * find(
  const std::vector<rclcpp::Parameter> & parameters, const std::string & name)
{
  for (auto entry = parameters.rbegin(); entry != parameters.rend(); ++entry) {
    if (entry->get_name() == name) {
      return &entry->get_parameter_value();
    }
  }
  return nullptr;
}

void expect(bool condition, const std::string & what)
{
  std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition) {
    ++failures;
  }
}

void expectDouble(
  const std::vector<rclcpp::Parameter> & parameters, const std::string & name, double want)
{
  const rclcpp::ParameterValue * value = find(parameters, name);
  const bool ok = value != nullptr && value->get_type() == rclcpp::PARAMETER_DOUBLE &&
    value->get_value<double>() == want;
  expect(ok, name + " == " + std::to_string(want));
}

void expectInteger(
  const std::vector<rclcpp::Parameter> & parameters, const std::string & name, int64_t want)
{
  const rclcpp::ParameterValue * value = find(parameters, name);
  const bool ok = value != nullptr && value->get_type() == rclcpp::PARAMETER_INTEGER &&
    value->get_value<int64_t>() == want;
  expect(ok, name + " == " + std::to_string(want));
}

void expectBool(
  const std::vector<rclcpp::Parameter> & parameters, const std::string & name, bool want)
{
  const rclcpp::ParameterValue * value = find(parameters, name);
  const bool ok = value != nullptr && value->get_type() == rclcpp::PARAMETER_BOOL &&
    value->get_value<bool>() == want;
  expect(ok, name + (want ? " == true" : " == false"));
}

void expectString(
  const std::vector<rclcpp::Parameter> & parameters, const std::string & name,
  const std::string & want)
{
  const rclcpp::ParameterValue * value = find(parameters, name);
  const bool ok = value != nullptr && value->get_type() == rclcpp::PARAMETER_STRING &&
    value->get_value<std::string>() == want;
  expect(ok, name + " == " + want);
}

std::string writeFile(const std::string & name, const std::string & text)
{
  std::ofstream out(name, std::ios::binary | std::ios::trunc);
  out << text;
  return name;
}

}  // namespace

int main()
{
  std::printf("params_file: shipped slam_toolbox config\n");
  {
    std::vector<rclcpp::Parameter> parameters;
    std::string error;
    const bool loaded = ros2_shim::loadParamsFile(
      PARAMS_TEST_SLAM_CONFIG, "slam_toolbox", "", parameters, &error);
    expect(loaded, "mapper_params_online_async.yaml loads" + (loaded ? "" : ": " + error));
    expectDouble(parameters, "resolution", 0.05);
    expectDouble(parameters, "map_update_interval", 5.0);
    expectInteger(parameters, "scan_buffer_size", 10);
    expectBool(parameters, "use_scan_matching", true);
    expectBool(parameters, "do_loop_closing", true);
    expectString(parameters, "base_frame", "base_footprint");
    expectString(parameters, "mode", "mapping");
    // `mode: mapping #localization` carries a comment; YAML strips it, and the
    // value must not read back as `mapping #localization`.
    // The same value is also reachable under the fully qualified name, which is
    // the other key the shim resolves an override by.
    expectDouble(parameters, "/slam_toolbox.resolution", 0.05);
    // The node's own `robot_model_type` is not in this file at all.
    expect(find(parameters, "alpha1") == nullptr, "no parameters from another file");
  }

  std::printf("params_file: nav2 bringup parameters\n");
  {
    std::vector<rclcpp::Parameter> parameters;
    std::string error;
    const bool loaded =
      ros2_shim::loadParamsFile(PARAMS_TEST_NAV2_PARAMS, "amcl", "", parameters, &error);
    expect(loaded, "nav2_params.yaml loads" + (loaded ? "" : ": " + error));
    expectDouble(parameters, "alpha1", 0.2);
    expectDouble(parameters, "laser_min_range", -1.0);
    expectInteger(parameters, "max_beams", 60);
    expectInteger(parameters, "random_seed", -1);
    expectBool(parameters, "do_beamskip", false);
    expectBool(parameters, "tf_broadcast", true);
    expectString(parameters, "robot_model_type", "nav2_amcl::DifferentialMotionModel");
    expectString(parameters, "scan_topic", "scan");
    // A bare `distribution` style name is not a number, so it stays a string;
    // `40.0` and `-1` above show the numeric cases are not strings.
    // Every other node in the file has to stay out of this one.
    expect(find(parameters, "robot_radius") == nullptr, "no controller_server parameters");
  }

  std::printf("params_file: the costmap blocks nav2_bringup nests one level down\n");
  {
    std::vector<rclcpp::Parameter> parameters;
    std::string error;
    const bool loaded = ros2_shim::loadParamsFile(
      PARAMS_TEST_NAV2_PARAMS, "local_costmap", "", parameters, &error);
    expect(loaded, "local_costmap loads" + (loaded ? "" : ": " + error));
    // The file spells this node's block `local_costmap: local_costmap:
    // ros__parameters:` - the outer key is the one nav2's launch files pull the
    // subtree out with, the inner one names the node - so the values have to
    // come out of the inner block, not out of nothing.
    expectString(parameters, "global_frame", "odom");
    expectBool(parameters, "rolling_window", true);
    expectDouble(parameters, "update_frequency", 5.0);
    expectDouble(parameters, "/local_costmap.resolution", 0.05);
    expectDouble(parameters, "inflation_layer.inflation_radius", 0.70);
    // The global costmap's block sits next to this one and must not leak in: it
    // is the one that asks for `track_unknown_space`.
    expect(find(parameters, "track_unknown_space") == nullptr, "no global_costmap parameters");
  }

  std::printf("params_file: the localisation flavour of the config\n");
  {
    std::vector<rclcpp::Parameter> parameters;
    std::string error;
    expect(
      ros2_shim::loadParamsFile(
        PARAMS_TEST_MAPPER_LOCALIZATION, "slam_toolbox", "", parameters, &error),
      "mapper_params_localization.yaml loads");
    expectString(parameters, "base_frame", "base_footprint");
    expectInteger(parameters, "scan_buffer_size", 3);
    // `#map_file_name: test_steve` and `#map_start_pose: [5.0, 1.0, 0.0]` are
    // commented out, so neither may turn into a parameter that would put the
    // node into localisation mode behind the caller's back.
    expect(find(parameters, "map_file_name") == nullptr, "a commented map_file_name stays inert");
    expect(find(parameters, "map_start_pose") == nullptr, "a commented map_start_pose stays inert");
  }

  std::printf("params_file: wildcards, nesting and arrays\n");
  {
    const std::string path = writeFile(
      "params_file_wildcards.yaml",
      "/**:\n"
      "  ros__parameters:\n"
      "    use_sim_time: true\n"
      "    resolution: 1.0\n"
      "    shared: from_the_wildcard\n"
      "slam_toolbox:\n"
      "  ros__parameters:\n"
      "    resolution: 0.05\n"
      "    mode: mapping\n"
      "    map_start_pose: [1.0, 2.0, 0.0]\n"
      "    initial_pose:\n"
      "      x: 1.5\n"
      "      y: -2.0\n"
      "other_node:\n"
      "  ros__parameters:\n"
      "    resolution: 9.0\n");
    std::vector<rclcpp::Parameter> parameters;
    std::string error;
    expect(ros2_shim::loadParamsFile(path, "slam_toolbox", "", parameters, &error), "file loads");
    // The block that names the node wins over `/**`, and the other node's block
    // is ignored even though it sets the same parameter.
    expectDouble(parameters, "resolution", 0.05);
    expectString(parameters, "shared", "from_the_wildcard");
    expectBool(parameters, "use_sim_time", true);
    // A nested map becomes the dotted name the nodes declare.
    expectDouble(parameters, "initial_pose.x", 1.5);
    expectDouble(parameters, "initial_pose.y", -2.0);
    const rclcpp::ParameterValue * pose = find(parameters, "map_start_pose");
    expect(
      pose != nullptr && pose->get_type() == rclcpp::PARAMETER_DOUBLE_ARRAY &&
      pose->get_value<std::vector<double>>().size() == 3,
      "map_start_pose is a three element double array");
    expect(find(parameters, "/other_node.resolution") == nullptr, "other node stays out");
    std::remove(path.c_str());
  }

  std::printf("params_file: namespaces and errors\n");
  {
    const std::string path = writeFile(
      "params_file_namespace.yaml",
      "/robot1/slam_toolbox:\n"
      "  ros__parameters:\n"
      "    resolution: 0.07\n"
      "/robot1/**:\n"
      "  ros__parameters:\n"
      "    mode: mapping\n");
    std::vector<rclcpp::Parameter> parameters;
    std::string error;
    expect(
      ros2_shim::loadParamsFile(path, "slam_toolbox", "robot1", parameters, &error),
      "a namespaced node reads /robot1/slam_toolbox");
    expectDouble(parameters, "resolution", 0.07);
    expectString(parameters, "mode", "mapping");

    std::vector<rclcpp::Parameter> global;
    std::string global_error;
    expect(
      ros2_shim::loadParamsFile(path, "slam_toolbox", "", global, &global_error),
      "the same file still loads for the global node");
    expect(global.empty(), "no block applies to a node in another namespace");
    std::remove(path.c_str());
  }

  std::printf("params_file: a missing file is an error, not a silent pass\n");
  {
    std::vector<rclcpp::Parameter> parameters;
    std::string error;
    expect(
      !ros2_shim::loadParamsFile("params_file_does_not_exist.yaml", "slam_toolbox", "", parameters,
        &error),
      "loading a missing file fails");
    expect(!error.empty(), "the failure explains itself");
    expect(parameters.empty(), "nothing was appended");
  }

  if (failures != 0) {
    std::printf("params_file: %d check(s) failed\n", failures);
    return 1;
  }
  std::printf("params_file: all checks passed\n");
  return 0;
}
