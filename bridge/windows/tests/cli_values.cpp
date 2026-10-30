// bridge_cli_values - the value syntax of `ros2 topic pub`, `ros2 service
// call` and `ros2 action send_goal`.
//
// Real ROS2 takes the last positional argument of those three subcommands as a
// YAML document ("Values to fill the message with in YAML format (e.g. 'data:
// Hello World')"), while this tree took a list of `field.path=value` tokens and
// nothing else:
//
//   ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.5}}"   # was rejected
//   ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.5           # always worked
//
// Both are accepted now and both end up in `applyFields`, so what this test
// pins down is the flattening (nesting, sequences, quotes, comments) and the
// errors that must not be swallowed - a value that cannot be represented is an
// error, never a silently different message.
//
// The last block runs the flattened result through the real codecs
// (`build_shim` + `describe_shim`), which is exactly what `ros2 topic pub`
// does, so the quote bug this started from (`data="hi"` publishing the quotes
// themselves) cannot come back unnoticed.

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ros2_bridge/action_registry.hpp"
#include "ros2_bridge/type_registry.hpp"
#include "ros2_bridge/yaml_assign.hpp"

namespace
{

int g_failures = 0;
int g_checks = 0;

namespace yaml_assign = ros2_bridge::yaml_assign;

// The expected value of every block below is the assignment list written the
// way the command line writes it: `a=1 b=2`, empty when nothing is emitted.
std::string render(const std::vector<ros2_bridge::FieldAssign> & fields)
{
  std::string out;
  for (const auto & field : fields) {
    if (!out.empty()) {
      out += " ";
    }
    out += field.path + "=" + field.value;
  }
  return out;
}

void failMessage(const std::string & message)
{
  ++g_failures;
  std::fprintf(stderr, "  FAIL %s\n", message.c_str());
}

void expectEqual(
  const std::string & what, const std::string & actual, const std::string & expected)
{
  ++g_checks;
  if (actual == expected) {
    return;
  }
  ++g_failures;
  std::fprintf(
    stderr, "  FAIL %s\n    actual:   %s\n    expected: %s\n", what.c_str(),
    actual.c_str(), expected.c_str());
}

void expectDocument(
  const std::string & what, const std::string & text, const std::string & expected)
{
  ++g_checks;
  std::vector<ros2_bridge::FieldAssign> fields;
  std::string error;
  if (!yaml_assign::parseDocument(text, fields, error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' was rejected: %s\n", what.c_str(), text.c_str(), error.c_str());
    return;
  }
  expectEqual(what, render(fields), expected);
}

void expectRejected(
  const std::string & what, const std::string & text, const std::string & needle)
{
  ++g_checks;
  std::vector<ros2_bridge::FieldAssign> fields;
  std::string error;
  if (yaml_assign::parseDocument(text, fields, error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' was accepted as '%s'\n", what.c_str(), text.c_str(),
      render(fields).c_str());
    return;
  }
  ++g_checks;
  if (error.find(needle) == std::string::npos) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' was rejected with '%s', which does not mention '%s'\n",
      what.c_str(), text.c_str(), error.c_str(), needle.c_str());
  }
}

void expectFlat(
  const std::string & what, const std::string & token, const std::string & expected)
{
  ++g_checks;
  std::vector<ros2_bridge::FieldAssign> fields;
  std::string error;
  if (!yaml_assign::parseFlatAssignment(token, fields, error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' was rejected: %s\n", what.c_str(), token.c_str(), error.c_str());
    return;
  }
  expectEqual(what, render(fields), expected);
}

const char * formName(yaml_assign::Form form)
{
  switch (form) {
    case yaml_assign::Form::Flat:
      return "Flat";
    case yaml_assign::Form::Document:
      return "Document";
    default:
      return "Neither";
  }
}

void expectForm(const std::string & token, yaml_assign::Form expected)
{
  ++g_checks;
  const yaml_assign::Form actual = yaml_assign::classify(token);
  if (actual == expected) {
    return;
  }
  ++g_failures;
  std::fprintf(
    stderr, "  FAIL classify('%s'): actual %s, expected %s\n", token.c_str(),
    formName(actual), formName(expected));
}

// The whole point of the exercise: the document reaches a real codec and comes
// back out of `describe_shim` the way `ros2 topic echo` prints it.
// A value as the command line hands it over: one YAML document (quoted, or a
// block) or a run of `field=value` tokens. `classify` is what
// tools/ros2_cli.cpp uses to tell the two apart, and this reads a value the
// same way, so `poses[0].pose.position.x=1.0` is covered through the same
// dispatch the command line uses instead of only through the parser.
bool readValue(
  const std::string & text, std::vector<ros2_bridge::FieldAssign> & fields, std::string & error)
{
  if (text.find('{') != std::string::npos || text.find(':') != std::string::npos) {
    return yaml_assign::parseDocument(text, fields, error);
  }
  std::size_t begin = 0;
  while (begin < text.size()) {
    const std::size_t end = text.find(' ', begin);
    const std::string token = text.substr(
      begin, end == std::string::npos ? std::string::npos : end - begin);
    begin = end == std::string::npos ? text.size() : end + 1;
    if (token.empty()) {
      continue;
    }
    if (yaml_assign::classify(token) != yaml_assign::Form::Flat ||
      !yaml_assign::parseFlatAssignment(token, fields, error))
    {
      error = "'" + token + "' is not a field assignment: " + error;
      return false;
    }
  }
  return true;
}

void expectBuild(
  const std::string & what, const std::string & type, const std::string & document,
  const std::string & needle)
{
  ++g_checks;
  std::vector<ros2_bridge::FieldAssign> fields;
  std::string error;
  if (!readValue(document, fields, error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' was rejected: %s\n", what.c_str(), document.c_str(),
      error.c_str());
    return;
  }
  ros2_bridge::Bytes shim;
  std::string build_error;
  if (!ros2_bridge::build_shim(type, fields, shim, &build_error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' did not build a %s: %s\n", what.c_str(), document.c_str(),
      type.c_str(), build_error.c_str());
    return;
  }
  const std::string described = ros2_bridge::describe_shim(type, shim);
  ++g_checks;
  if (described.find(needle) == std::string::npos) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: built\n    %s  but the message did not contain '%s'\n", what.c_str(),
      described.c_str(), needle.c_str());
  }
}

void expectBuildRejected(
  const std::string & what, const std::string & type, const std::string & document,
  const std::string & needle)
{
  ++g_checks;
  std::vector<ros2_bridge::FieldAssign> fields;
  std::string error;
  if (!readValue(document, fields, error)) {
    // Rejected before it ever reached the codec; the message still has to say
    // why, so fall through to the same check.
    ++g_checks;
    if (error.find(needle) == std::string::npos) {
      ++g_failures;
      std::fprintf(
        stderr, "  FAIL %s: '%s' was rejected with '%s', which does not mention '%s'\n",
        what.c_str(), document.c_str(), error.c_str(), needle.c_str());
    }
    return;
  }
  ros2_bridge::Bytes shim;
  std::string build_error;
  if (ros2_bridge::build_shim(type, fields, shim, &build_error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' built a %s instead of failing\n", what.c_str(),
      document.c_str(), type.c_str());
    return;
  }
  ++g_checks;
  if (build_error.find(needle) == std::string::npos) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' failed with '%s', which does not mention '%s'\n", what.c_str(),
      document.c_str(), build_error.c_str(), needle.c_str());
  }
}

// One goal of an action, from the same document `ros2 action send_goal` takes,
// read back through the codec the command uses. A sequence of messages lives in
// the goal of an action or in a `nav_msgs/msg/Path`, so this is where the two
// halves of the spelling meet.
void expectGoal(
  const std::string & what, const std::string & action, const std::string & document,
  const std::string & needle)
{
  ++g_checks;
  std::vector<ros2_bridge::FieldAssign> fields;
  std::string error;
  if (!readValue(document, fields, error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' was rejected: %s\n", what.c_str(), document.c_str(),
      error.c_str());
    return;
  }
  const ros2_bridge::ActionCodec * codec = ros2_bridge::find_action_codec(action);
  if (codec == nullptr || !codec->build_goal) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s: %s has no goal codec\n", what.c_str(), action.c_str());
    return;
  }
  ros2_bridge::Bytes goal;
  std::string build_error;
  if (!codec->build_goal(fields, goal, build_error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' did not build: %s\n", what.c_str(), document.c_str(),
      build_error.c_str());
    return;
  }
  const std::string described = codec->describe_goal(goal);
  if (described.find(needle) == std::string::npos) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: built\n    %s  but the goal does not contain '%s'\n", what.c_str(),
      described.c_str(), needle.c_str());
  }
}

void expectGoalRejected(
  const std::string & what, const std::string & action, const std::string & document,
  const std::string & needle)
{
  ++g_checks;
  std::vector<ros2_bridge::FieldAssign> fields;
  std::string error;
  if (!readValue(document, fields, error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' was rejected: %s\n", what.c_str(), document.c_str(),
      error.c_str());
    return;
  }
  const ros2_bridge::ActionCodec * codec = ros2_bridge::find_action_codec(action);
  if (codec == nullptr || !codec->build_goal) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s: %s has no goal codec\n", what.c_str(), action.c_str());
    return;
  }
  ros2_bridge::Bytes goal;
  std::string build_error;
  if (codec->build_goal(fields, goal, build_error)) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' built a goal instead of failing\n", what.c_str(),
      document.c_str());
    return;
  }
  if (build_error.find(needle) == std::string::npos) {
    ++g_failures;
    std::fprintf(
      stderr, "  FAIL %s: '%s' failed with '%s', which does not mention '%s'\n", what.c_str(),
      document.c_str(), build_error.c_str(), needle.c_str());
  }
}

}  // namespace

int main()
{
  // Which form a token is. A topic or type name must stay Neither or the two
  // positional arguments get eaten by the value parser.
  expectForm("/cmd_vel", yaml_assign::Form::Neither);
  expectForm("/sim/laser_scan", yaml_assign::Form::Neither);
  expectForm("geometry_msgs/msg/Twist", yaml_assign::Form::Neither);
  expectForm("std_msgs/msg/String", yaml_assign::Form::Neither);
  expectForm("--once", yaml_assign::Form::Neither);
  expectForm("linear.x=0.5", yaml_assign::Form::Flat);
  expectForm("data=hi", yaml_assign::Form::Flat);
  expectForm("data=\"hi\"", yaml_assign::Form::Flat);
  expectForm("linear={x: 1}", yaml_assign::Form::Flat);
  expectForm("ranges=[1.0, 2.0]", yaml_assign::Form::Flat);
  expectForm("data=a:b", yaml_assign::Form::Flat);
  expectForm("poses[0].pose.position.x=1", yaml_assign::Form::Flat);
  expectForm("poses.goals[2].header.frame_id=map", yaml_assign::Form::Flat);
  expectForm("{linear: {x: 0.5}}", yaml_assign::Form::Document);
  expectForm("linear: {x: 0.5}", yaml_assign::Form::Document);
  expectForm("data: Hello World", yaml_assign::Form::Document);
  expectForm("{}", yaml_assign::Form::Document);
  expectForm("data: a=b", yaml_assign::Form::Document);

  // A shell that splits the document hands the pieces over one argument at a
  // time; the depth is what tells the caller to keep collecting them.
  expectEqual("openDepth whole", std::to_string(yaml_assign::openDepth("{linear: {x: 0.5}}")), "0");
  expectEqual("openDepth first", std::to_string(yaml_assign::openDepth("{linear:")), "1");
  expectEqual("openDepth two", std::to_string(yaml_assign::openDepth("{linear: {x:")), "2");
  expectEqual(
    "openDepth quoted brace", std::to_string(yaml_assign::openDepth("data: '{'")), "0");

  // The documents real ROS2 documents, and the nested spellings that are the
  // reason this exists.
  expectDocument("twist", "{linear: {x: 0.5}}", "linear.x=0.5");
  expectDocument("twist both", "{linear: {x: 0.5}, angular: {z: -0.2}}", "linear.x=0.5 angular.z=-0.2");
  expectDocument("flat pair", "{a: 1, b: 2}", "a=1 b=2");
  expectDocument("three deep", "{pose: {position: {x: 1, y: 2}}}", "pose.position.x=1 pose.position.y=2");
  expectDocument("no spaces", "{linear:{x:0.5}}", "linear.x=0.5");
  expectDocument("trailing comma", "{a: 1,}", "a=1");
  expectDocument("empty map", "{}", "");
  expectDocument("sequence", "{ranges: [1.0, 2.0, 3.0]}", "ranges=1.0, 2.0, 3.0");
  expectDocument("sequence no spaces", "{ranges: [1,2,3]}", "ranges=1, 2, 3");
  expectDocument("empty sequence", "{ranges: []}", "ranges=");
  expectDocument("quoted item", "{names: ['a b', c]}", "names=a b, c");
  expectDocument("sequence of mappings", "{points: [{x: 1}, {y: 2}]}", "points[0].x=1 points[1].y=2");
  expectDocument("sequence of one", "{points: [{x: 1}]}", "points[0].x=1");
  expectDocument(
    "waypoint list",
    "{poses: {header: {frame_id: map}, goals: [{pose: {position: {x: 1}}},"
    " {header: {frame_id: odom}, pose: {position: {y: 2}}}]}}",
    "poses.header.frame_id=map poses.goals[0].pose.position.x=1"
    " poses.goals[1].header.frame_id=odom poses.goals[1].pose.position.y=2");
  expectDocument(
    "quoted indexed key", "{'poses.goals[1].header.stamp.sec': 7}",
    "poses.goals[1].header.stamp.sec=7");
  expectDocument("double quoted", "{data: \"hi\"}", "data=hi");
  expectDocument("single quoted", "{data: 'hi'}", "data=hi");
  expectDocument("escaped quote", "{data: 'it''s'}", "data=it's");
  expectDocument("escaped newline", "{data: \"a\\nb\"}", std::string("data=a\nb"));
  expectDocument("inner quotes", "{data: '\"hi\"'}", "data=\"hi\"");
  // A comment runs to the end of the line, so a closing brace on the same
  // line is commented out too - that is YAML, not this parser.
  expectDocument("comment in a document", "{\n  frame_id: map # the map frame\n}", "frame_id=map");
  expectDocument("comment on a bare pair", "frame_id: map # the map frame", "frame_id=map");
  expectRejected("brace in a comment", "{frame_id: map # the map frame}", "unterminated '{'");
  expectDocument("comment line", "# nothing here\ndata: hi", "data=hi");
  expectDocument("bare pair", "data: Hello World", "data=Hello World");
  expectDocument("bare pair list", "ranges: [1.0, 2.0]", "ranges=1.0, 2.0");
  expectDocument(
    "bare two pairs", "linear: {x: 0.5}, angular: {z: 1}", "linear.x=0.5 angular.z=1");
  expectDocument("comma in text", "data: hi, there", "data=hi, there");
  expectDocument("comma then pair", "data: hi, id: 2", "data=hi id=2");
  expectDocument("quoted key", "{\"frame_id\": map}", "frame_id=map");
  expectDocument("keys with dots", "{linear.x: 0.5}", "linear.x=0.5");
  expectDocument("empty value", "{a: }", "a=");
  expectDocument("null", "{data: null}", "data=");
  expectDocument("tilde", "{data: ~}", "data=");
  expectDocument("typed null", "{data: NULL}", "data=");
  expectDocument(
    "multiline document", "{\n  linear: {x: 0.5},\n  angular: {z: 1},\n}",
    "linear.x=0.5 angular.z=1");

  // Block style - the other shape YAML is written in, and the one a value
  // copied out of a params file arrives in. A key with nothing after its `:`
  // opens the block indented under it; a bare scalar ends with its line, which
  // is what stops `data: hi` plus a following `other: 1` from becoming the one
  // value `hi\nother: 1`.
  expectDocument("block nested", "linear:\n  x: 0.5", "linear.x=0.5");
  expectDocument(
    "block two parents", "linear:\n  x: 0.5\nangular:\n  z: -0.2",
    "linear.x=0.5 angular.z=-0.2");
  expectDocument(
    "block inline and nested", "linear: {x: 0.5}\nangular:\n  z: 1",
    "linear.x=0.5 angular.z=1");
  expectDocument(
    "block plain scalars", "frame_id: map\nchild_frame_id: base_link",
    "frame_id=map child_frame_id=base_link");
  expectDocument("block ends at the line", "data: hi\nid: 2", "data=hi id=2");
  expectDocument("block comment between keys", "a: 1\n# note\nb: 2", "a=1 b=2");
  expectDocument("block comment after value", "data: hi # there\nid: 2", "data=hi id=2");
  expectDocument("block deeper indent", "  linear:\n      x: 0.5", "linear.x=0.5");
  expectDocument("block null then sibling", "a:\nb: 1", "a= b=1");
  expectDocument("block quoted scalar", "data: 'hi there'", "data=hi there");
  expectDocument("block crlf", "data: hi\r\nid: 2", "data=hi id=2");

  // Everything that cannot be turned into a flat field list is an error with
  // an explanation - never a silently different message.
  expectRejected("mixed sequence", "{points: [1, {x: 1}]}", "mixes values and messages");
  expectRejected("sequence inside a sequence", "{points: [[1]]}", "sequence inside a sequence");
  expectRejected("bare sequence", "[1, 2, 3]", "not a bare sequence");
  expectRejected("unterminated brace", "{linear: {x: 0.5}", "unterminated '{'");
  expectRejected("stray brace", "{ranges: [1, 2}", "unexpected '}' inside the sequence of 'ranges'");
  expectRejected("unterminated sequence", "{ranges: [1, 2", "unterminated '['");
  expectRejected("missing colon", "{a 1}", "expected ':'");
  expectRejected("missing value", "{a:", "expected a value after the ':' of 'a'");
  expectRejected("trailing junk", "{a: 1} and more", "after the YAML value");
  expectRejected("unterminated quote", "{data: \"hi}", "unterminated quoted value");
  expectRejected("empty", "", "empty YAML value");
  expectRejected("block bad indent", "a: 1\n  b: 2", "bad indentation");
  expectRejected("block missing colon", "hello", "expected ':'");
  expectRejected("block junk after a flow value", "linear: {x: 0.5} junk", "after the value of");

  // The flat form: unchanged for everything that already worked, plus the
  // quote and nesting spellings a shell hands over whole.
  expectFlat("scalar", "data=hi", "data=hi");
  expectFlat("float", "linear.x=0.5", "linear.x=0.5");
  expectFlat("negative", "angular.z=-0.2", "angular.z=-0.2");
  expectFlat("legacy list", "ranges=1.0,2.0,3.0", "ranges=1.0,2.0,3.0");
  expectFlat(
    "indexed assignment", "poses[0].pose.position.x=1.0", "poses[0].pose.position.x=1.0");
  expectFlat(
    "indexed assignment with a list", "poses.goals=[{pose: {position: {x: 1}}}]",
    "poses.goals[0].pose.position.x=1");
  expectFlat("quoted scalar", "data=\"hi\"", "data=hi");
  expectFlat("quoted scalar single", "data='hi'", "data=hi");
  expectFlat("quoted with comma", "data=\"hi, there\"", "data=hi, there");
  expectFlat("quoted empty", "data=\"\"", "data=");
  expectFlat("empty value", "data=", "data=");
  expectFlat("sequence value", "ranges=[1.0, 2.0, 3.0]", "ranges=1.0, 2.0, 3.0");
  expectFlat("nested value", "linear={x: 1, y: 2}", "linear.x=1 linear.y=2");
  expectFlat("lone quote", "data=a\"b", "data=a\"b");
  expectFlat("quote in middle", "data=it's", "data=it's");

  // End to end through the codecs that `ros2 topic pub` uses.
  expectBuild("string yaml", "std_msgs/msg/String", "{data: hi}", "data: \"hi\"");
  expectBuild("string quoted", "std_msgs/msg/String", "{data: \"hi\"}", "data: \"hi\"");
  expectBuild("string comma", "std_msgs/msg/String", "{data: \"hi, there\"}", "data: \"hi, there\"");
  expectBuild("string empty", "std_msgs/msg/String", "{data: null}", "data: \"\"");
  expectBuild("bool yaml", "std_msgs/msg/Bool", "{data: true}", "data: true");
  expectBuild("bool false", "std_msgs/msg/Bool", "{data: false}", "data: false");
  expectBuild("twist nested", "geometry_msgs/msg/Twist", "{linear: {x: 0.5}}", "linear: {x: 0.5");
  expectBuild("twist dotted", "geometry_msgs/msg/Twist", "{'linear.x': 0.5}", "linear: {x: 0.5");
  expectBuild(
    "twist full", "geometry_msgs/msg/Twist",
    "{linear: {x: 0.5, y: 0.1}, angular: {z: -0.2}}", "angular: {x: 0, y: 0, z: -0.2");
  expectBuild(
    "scan ranges", "sensor_msgs/msg/LaserScan", "{ranges: [1.0, 2.0, 3.0]}",
    "ranges: [3 values, min 1, max 3]");
  expectBuild(
    "scan scalars", "sensor_msgs/msg/LaserScan", "{angle_min: -1.5, range_max: 12.0}",
    "range_max: 12");
  expectBuild(
    "twist block", "geometry_msgs/msg/Twist", "linear:\n  x: 0.5\nangular:\n  z: -0.2",
    "angular: {x: 0, y: 0, z: -0.2");
  expectBuild("string block", "std_msgs/msg/String", "data: hello world", "data: \"hello world\"");
  expectBuild(
    "tf convenience", "tf2_msgs/msg/TFMessage",
    "{frame_id: odom, child_frame_id: base_link, x: 1}", "child_frame_id: \"base_link\"");
  // A published plan: the one message on this graph whose main field is a
  // sequence of messages, and the spelling `ros2 topic pub /plan` needs.
  expectBuild(
    "path poses", "nav_msgs/msg/Path",
    "{header: {frame_id: map}, poses: [{pose: {position: {x: 1.0, y: 2.0}}},"
    " {pose: {position: {x: 3.0, y: 4.0}}}]}",
    "poses: [2 waypoints]");
  expectBuild(
    "path indexed", "nav_msgs/msg/Path", "poses[1].pose.position.x=3.5",
    "poses: [2 waypoints]");
  expectGoal(
    "navigate through poses", "nav2_msgs/action/NavigateThroughPoses",
    "{poses: {header: {frame_id: map}, goals: [{pose: {position: {x: 1.0}}},"
    " {pose: {position: {x: 2.0}}}]}}",
        "poses: 2 goal(s) in \"map\"");
  expectGoal(
    "navigate through poses indexed", "nav2_msgs/action/NavigateThroughPoses",
    "poses.header.frame_id=map poses.goals[0].pose.position.x=1.0"
    " poses.goals[0].pose.orientation.w=1.0 poses.goals[2].pose.position.x=3.0",
        "poses: 3 goal(s) in \"map\"");
  expectBuildRejected(
    "block second field", "std_msgs/msg/String", "data: hi\nfoo: 1", "no settable field 'foo'");
  expectBuildRejected(
    "unknown field", "geometry_msgs/msg/Twist", "{nope: 1}", "no settable field 'nope'");
  expectBuildRejected(
    "field of another type", "geometry_msgs/msg/Twist", "{width: 1}", "no settable field");
  expectBuildRejected(
    "sequence of maps in a scalar list", "sensor_msgs/msg/LaserScan", "{ranges: [{x: 1}]}",
    "no settable field 'ranges[0].x'");
  expectBuildRejected(
    "a message list written without an index", "nav_msgs/msg/Path", "{poses: 1.0}",
    "'poses' carries messages, one per element");
  expectGoalRejected(
    "unknown waypoint field", "nav2_msgs/action/NavigateThroughPoses",
    "poses.goals[0].pose.position.nope=1", "a waypoint has no settable field");

  if (g_failures != 0) {
    std::fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
    return 1;
  }
  std::printf("cli values: %d checks passed\n", g_checks);
  return 0;
}
