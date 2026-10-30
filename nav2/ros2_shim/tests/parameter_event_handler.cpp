// `ParameterEventHandler` is the one rclcpp API whose whole meaning is
// delivery: which change reaches which callback. That is what is checked here,
// because a handler that never fires and a handler that fires on everything
// look identical from the outside - Smac simply keeps planning against a stale
// costmap resolution.
//
//   1. a callback registered for one node and one parameter fires on that, and
//      on neither an unrelated parameter nor another node,
//   2. node names match with or without the leading slash, because nav2 builds
//      the string both ways,
//   3. only an applied set is announced - a set the node rejected is not,
//   4. unregistering works, by call and by dropping the handle,
//   5. a callback may read the node's parameters, which is what Smac's does;
//      this is the one that hangs the process rather than failing an assertion,
//      if the announcement is ever made while the node's parameter mutex is
//      held,
//   6. an empty node name watches every node,
//   7. and the string nav2 actually passes - `global_costmap/global_costmap` -
//      matches nothing, which is upstream's behaviour too and is pinned here so
//      that a change to it is a decision rather than a surprise.
//
// Each node built here also opens its `/parameter_events` publisher, because
// that is what a node does unless it is told not to; the handler's own delivery
// is independent of it (see `detail::ParameterEventHub`), and
// tests/parameter_events.cpp is where the topic itself is checked.
#include "rclcpp/parameter_event_handler.hpp"

#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

#include <cstdio>
#include <memory>
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

void section(const char * name)
{
  std::printf("%s\n", name);
}

/// Everything a callback was handed, in order.
struct Recorded
{
  std::vector<std::string> names;
  std::vector<double> values;

  void operator()(const rclcpp::Parameter & parameter)
  {
    names.push_back(parameter.get_name());
    values.push_back(parameter.as_double());
  }

  void clear()
  {
    names.clear();
    values.clear();
  }

  size_t count() const {return names.size();}
};

/// The Smac shape: the handler is built from the planner's node, and the
/// callback watches a *different* node by name.
void callbackFiresOnlyForTheNodeAndParameterItNamed()
{
  section("a callback fires for the node and the parameter it named");

  auto costmap = std::make_shared<rclcpp::Node>("global_costmap");
  auto other = std::make_shared<rclcpp::Node>("local_costmap");
  auto handler = std::make_shared<rclcpp::ParameterEventHandler>(costmap);

  Recorded resolution;
  auto handle = handler->add_parameter_callback(
    "resolution", [&resolution](const rclcpp::Parameter & p) {resolution(p);}, "global_costmap");

  costmap->set_parameter(rclcpp::Parameter("resolution", 0.05));
  expect(resolution.count() == 1, "the named node's parameter fires it once");
  expect(resolution.count() == 1 && resolution.values[0] == 0.05, "and hands over the new value");

  costmap->set_parameter(rclcpp::Parameter("footprint_padding", 0.03));
  expect(resolution.count() == 1, "an unrelated parameter on the same node does not");

  other->set_parameter(rclcpp::Parameter("resolution", 0.10));
  expect(resolution.count() == 1, "the same parameter on another node does not");

  section("the node name matches with or without a leading slash");
  resolution.clear();
  // The registered name is bare and the node reports itself as
  // `/global_costmap` - the two are one node.
  costmap->set_parameter(rclcpp::Parameter("resolution", 0.06));
  expect(resolution.count() == 1, "still fires when the spellings differ by the slash");

  Recorded slashed;
  auto slashed_handle = handler->add_parameter_callback(
    "resolution", [&slashed](const rclcpp::Parameter & p) {slashed(p);}, "/global_costmap");
  costmap->set_parameter(rclcpp::Parameter("resolution", 0.07));
  expect(slashed.count() == 1, "a leading slash on the registered name matches too");

  section("the string nav2 hardcodes matches nothing, as upstream");
  Recorded nav2_spelling;
  auto nav2_handle = handler->add_parameter_callback(
    "resolution", [&nav2_spelling](const rclcpp::Parameter & p) {nav2_spelling(p);},
    "global_costmap/global_costmap");
  costmap->set_parameter(rclcpp::Parameter("resolution", 0.08));
  expect(
    nav2_spelling.count() == 0,
    "`global_costmap/global_costmap` is not `global_costmap`; upstream agrees");
}

void onlyAnAppliedSetIsAnnounced()
{
  section("only a set the node applied is announced");

  auto node = std::make_shared<rclcpp::Node>("rejecting_node");
  auto handler = std::make_shared<rclcpp::ParameterEventHandler>(node);

  // Rejects anything above 1.0, the way a range-checking plugin does.
  node->add_on_set_parameters_callback(
    [](const std::vector<rclcpp::Parameter> & parameters) {
      rcl_interfaces::msg::SetParametersResult result;
      result.successful = true;
      for (const auto & parameter : parameters) {
        if (parameter.as_double() > 1.0) {
          result.successful = false;
          result.reason = "too large";
        }
      }
      return result;
    });

  Recorded seen;
  auto handle = handler->add_parameter_callback(
    "resolution", [&seen](const rclcpp::Parameter & p) {seen(p);}, "rejecting_node");

  auto accepted = node->set_parameter(rclcpp::Parameter("resolution", 0.5));
  expect(accepted.successful, "the accepted set succeeds");
  expect(seen.count() == 1, "and is announced");

  auto rejected = node->set_parameter(rclcpp::Parameter("resolution", 5.0));
  expect(!rejected.successful, "the rejected set fails");
  expect(seen.count() == 1, "and is not announced - nothing changed to announce");
}

void unregisteringWorks()
{
  section("unregistering, by call and by dropping the handle");

  auto node = std::make_shared<rclcpp::Node>("unregister_node");
  auto handler = std::make_shared<rclcpp::ParameterEventHandler>(node);

  Recorded seen;
  auto handle = handler->add_parameter_callback(
    "resolution", [&seen](const rclcpp::Parameter & p) {seen(p);}, "unregister_node");

  node->set_parameter(rclcpp::Parameter("resolution", 0.05));
  expect(seen.count() == 1, "registered, so it fires");

  expect(handler->remove_parameter_callback(handle), "remove_parameter_callback reports success");
  node->set_parameter(rclcpp::Parameter("resolution", 0.06));
  expect(seen.count() == 1, "and it stops firing");
  expect(!handler->remove_parameter_callback(handle), "removing twice reports nothing to remove");

  {
    auto scoped = handler->add_parameter_callback(
      "resolution", [&seen](const rclcpp::Parameter & p) {seen(p);}, "unregister_node");
  }
  node->set_parameter(rclcpp::Parameter("resolution", 0.07));
  expect(seen.count() == 1, "a handle that went out of scope unregisters itself");
}

/// Would hang, not fail, if the announcement were made under the node's
/// parameter mutex: `get_parameter` takes the same plain `std::mutex`.
void aCallbackMayReadTheNodesParameters()
{
  section("a callback may read the node's parameters");

  auto node = std::make_shared<rclcpp::Node>("reading_node");
  auto handler = std::make_shared<rclcpp::ParameterEventHandler>(node);

  double read_back = -1.0;
  auto handle = handler->add_parameter_callback(
    "resolution",
    [&node, &read_back](const rclcpp::Parameter &) {
      double value = 0.0;
      if (node->get_parameter("resolution", value)) {
        read_back = value;
      }
    },
    "reading_node");

  node->set_parameter(rclcpp::Parameter("resolution", 0.07));
  expect(read_back == 0.07, "the callback sees the value the set applied");
}

void anEmptyNodeNameWatchesEveryNode()
{
  section("an empty node name watches every node");

  auto first = std::make_shared<rclcpp::Node>("watched_first");
  auto second = std::make_shared<rclcpp::Node>("watched_second");
  auto handler = std::make_shared<rclcpp::ParameterEventHandler>(first);

  Recorded seen;
  auto handle = handler->add_parameter_callback(
    "resolution", [&seen](const rclcpp::Parameter & p) {seen(p);});

  first->set_parameter(rclcpp::Parameter("resolution", 0.05));
  second->set_parameter(rclcpp::Parameter("resolution", 0.06));
  expect(seen.count() == 2, "both nodes' sets arrive");
  expect(seen.count() == 2 && seen.values[0] == 0.05 && seen.values[1] == 0.06, "in order");

  section("the handler takes a lifecycle node, as the ported planner passes one");
  auto lifecycle = std::make_shared<rclcpp_lifecycle::LifecycleNode>("lifecycle_watched");
  auto lifecycle_handler = std::make_shared<rclcpp::ParameterEventHandler>(lifecycle);

  Recorded lifecycle_seen;
  auto lifecycle_handle = lifecycle_handler->add_parameter_callback(
    "resolution",
    [&lifecycle_seen](const rclcpp::Parameter & p) {lifecycle_seen(p);}, "lifecycle_watched");

  lifecycle->set_parameter(rclcpp::Parameter("resolution", 0.09));
  expect(lifecycle_seen.count() == 1, "lifecycle_watched_node's set arrives");
}

}  // namespace

int main()
{
  callbackFiresOnlyForTheNodeAndParameterItNamed();
  onlyAnAppliedSetIsAnnounced();
  unregisteringWorks();
  aCallbackMayReadTheNodesParameters();
  anEmptyNodeNameWatchesEveryNode();

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
