// `-p NAME:=VALUE`: what a node ends up with.
//
// `remap.cpp` next door pins which *name* a node ends up on; this file pins the
// value side of the same command line. The shape that needs pinning is the one
// a parameter made of many values takes, `[a, b, c]`, because it is the only
// shape a bare word cannot express:
//
//   * `slam_toolbox` reads its starting pose out of `map_start_pose`, a three
//     element double array, and the config file it ships writes it as
//     `map_start_pose: [5.0, 1.0, 0.0]`,
//   * nav2's plugin and filter lists are string arrays,
//   * and `ros2 param set` has always taken the list form, so a node that
//     honoured it from a terminal and not from a `-p` would make the two
//     spellings of the same thing disagree.
//
// What this file exists to make impossible to reintroduce: the value arriving
// as the *string* `"[0.0,0.0,0.0]"`, and then being dropped, because a dropped
// override reads exactly like an override nobody gave. `-p map_start_pose:=
// [0.0,0.0,0.0]` used to leave a localisation node logging "Map starting pose
// not specified" while the caller looked at a command line that plainly does
// specify it (bridge/docs/03-validation.md, evidence 47).
//
//   1. the scalars still read the way they always did: bool, integer, double,
//      string,
//   2. `[0.0, 0.0, 0.0]` reaches a parameter declared as a double array, which
//      is the `map_start_pose` case,
//   3. the elements pick the array type: integers -> `integer_array`, doubles
//      -> `double_array`, `true`/`false` -> `bool_array`, anything else ->
//      `string_array`, and a quoted element stays a string,
//   4. a `[` that is not the whole value is an ordinary character, so a frame
//      name or a path that contains one is still the string it looks like,
//   5. the runners' own `-p` (`ros2_shim::parseParameterSpec`, the function a
//      `.bat` reaches through `slam_bridge_node.exe` and the other runners)
//      reads a value the same way, because there is one reader,
//   6. and an override the declared type cannot read does not stop the node:
//      it starts on the value it declared. That is deliberate (see
//      `resolve_parameter_value` in `rclcpp/node.hpp`), and the other half of
//      the change - the warning that names the parameter instead of dropping
//      it in silence - is not something a test can see.
#include "rclcpp/node.hpp"
#include "rclcpp/node_options.hpp"
#include "ros2_shim/lifecycle_runner.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const std::string & what)
{
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++failures;
  }
}

// A node built the way a `.bat` builds one: the parameter specs on the command
// line, `-p` in front of each, behind the `--ros-args` separator `ros2 run`
// leaves there.
rclcpp::NodeOptions optionsWith(const std::vector<std::string> & specs)
{
  std::vector<std::string> arguments{"--ros-args"};
  for (const std::string & spec : specs) {
    arguments.push_back("-p");
    arguments.push_back(spec);
  }
  rclcpp::NodeOptions options;
  options.arguments(arguments);
  return options;
}

// Names both values, so a regression says what arrived rather than only that
// something did not.
void expectValue(
  const rclcpp::ParameterValue & got, const rclcpp::ParameterValue & want,
  const std::string & what)
{
  if (got == want) {
    return;
  }
  std::printf(
    "  FAIL %s: got %s, wanted %s\n", what.c_str(), got.value_to_string().c_str(),
    want.value_to_string().c_str());
  ++failures;
}

void test_scalars()
{
  rclcpp::Node node(
    "param_args_scalars",
    optionsWith({"flag:=true", "count:=7", "gain:=1.5", "label:=hello"}));

  expectValue(
    node.declare_parameter("flag", false), rclcpp::ParameterValue(true),
    "`true` still reads as a bool");
  expectValue(
    node.declare_parameter("count", 0), rclcpp::ParameterValue(int64_t{7}),
    "a bare number still reads as an integer");
  expectValue(
    node.declare_parameter("gain", 0.0), rclcpp::ParameterValue(1.5),
    "a decimal still reads as a double");
  expectValue(
    node.declare_parameter("label", std::string("")), rclcpp::ParameterValue(std::string("hello")),
    "a word still reads as a string");
}

void test_lists()
{
  rclcpp::Node node(
    "param_args_lists",
    optionsWith({
      // The map_start_pose case, character for character.
      "pose:=[0.0,0.0,0.0]",
      // Spaces around the elements are what a config file looks like when
      // somebody writes it out on the command line.
      "spaced:=[ 1.5 , -2.5 ]",
      "hits:=[1,2]",
      "flags:=[true,false]",
      "plugins:=[nav2_navfn_planner, nav2_smac_planner]",
      "quoted:=['1','2']",
    }));

  expectValue(
    node.declare_parameter("pose", std::vector<double>()),
    rclcpp::ParameterValue(std::vector<double>{0.0, 0.0, 0.0}),
    "`[0.0,0.0,0.0]` reaches a double array");
  expectValue(
    node.declare_parameter("spaced", std::vector<double>()),
    rclcpp::ParameterValue(std::vector<double>{1.5, -2.5}),
    "spaces and a sign between the elements do not matter");
  expectValue(
    node.declare_parameter("hits", std::vector<int64_t>()),
    rclcpp::ParameterValue(std::vector<int64_t>{1, 2}),
    "integer elements give an integer array");
  expectValue(
    node.declare_parameter("flags", std::vector<bool>()),
    rclcpp::ParameterValue(std::vector<bool>{true, false}),
    "`true`/`false` elements give a bool array");
  expectValue(
    node.declare_parameter("plugins", std::vector<std::string>()),
    rclcpp::ParameterValue(
      std::vector<std::string>{"nav2_navfn_planner", "nav2_smac_planner"}),
    "bare words give a string array, trimmed");
  expectValue(
    node.declare_parameter("quoted", std::vector<std::string>()),
    rclcpp::ParameterValue(std::vector<std::string>{"1", "2"}),
    "a quoted element is a string even when it looks like a number");
}

void test_brackets_that_are_not_a_list()
{
  rclcpp::Node node(
    "param_args_brackets",
    optionsWith({"frame_id:=base[1]", "path:=C:/maps/a[2]/walls.yaml", "tail:=x]"}));

  expectValue(
    node.declare_parameter("frame_id", std::string("")),
    rclcpp::ParameterValue(std::string("base[1]")),
    "a `[` inside a name stays text");
  expectValue(
    node.declare_parameter("path", std::string("")),
    rclcpp::ParameterValue(std::string("C:/maps/a[2]/walls.yaml")),
    "and inside a path");
  expectValue(
    node.declare_parameter("tail", std::string("")),
    rclcpp::ParameterValue(std::string("x]")),
    "and a `]` with no `[` in front of it is text too");
}

// The runners do not read their command line through a node: `-p` is taken by
// `takeParameterArgument`/`parseParameterSpec` first and handed to the node as
// a `NodeOptions` override. Same reader, and this is the call a `.bat` ends up
// in.
void test_runner_parameter_spec()
{
  rclcpp::Parameter parameter;
  expect(
    ros2_shim::parseParameterSpec(
      "map_start_pose:=[0.0,0.0,0.0]", parameter, "test_param_args"),
    "the runners' `-p` takes a list");
  expect(parameter.get_name() == "map_start_pose", "the name is what precedes `:=`");
  expectValue(
    parameter.get_value(), rclcpp::ParameterValue(std::vector<double>{0.0, 0.0, 0.0}),
    "and the value is the list");

  // `[]` has no element type `rcl_interfaces` can express, so it is refused
  // rather than turned into an empty array nobody notices; the same is true of
  // a spec with no `:=` in it at all.
  rclcpp::Parameter refused;
  expect(!ros2_shim::parseParameterSpec("plugins:=[]", refused, "test_param_args"),
    "`[]` is refused");
  expect(!ros2_shim::parseParameterSpec("bare_word", refused, "test_param_args"),
    "a spec without `:=` is refused");
  expect(refused.get_name().empty(), "and nothing is left in the parameter");
}

void test_a_value_the_declared_type_cannot_read()
{
  // Three integers where `slam_toolbox` declares a double array. That is a
  // configuration mistake, not a reason to abort: the node keeps the default it
  // declared and runs. The warning that names the parameter is the other half
  // of this - `resolve_parameter_value` in `rclcpp/node.hpp`.
  rclcpp::Node node("param_args_mismatch", optionsWith({"pose:=[0,0,0]"}));

  expectValue(
    node.declare_parameter("pose", std::vector<double>()),
    rclcpp::ParameterValue(std::vector<double>()),
    "an override the declared type cannot read keeps the declared default");
  expect(
    node.get_parameter("pose").as_double_array().empty(),
    "and the parameter is still readable as what it was declared with");
}

}  // namespace

int main()
{
  std::printf("=== -p NAME:=VALUE ===\n");
  rclcpp::init(0, nullptr);
  test_scalars();
  test_lists();
  test_brackets_that_are_not_a_list();
  test_runner_parameter_spec();
  test_a_value_the_declared_type_cannot_read();
  rclcpp::shutdown();

  if (failures != 0) {
    std::printf("param args: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("param args: all checks passed\n");
  return 0;
}
