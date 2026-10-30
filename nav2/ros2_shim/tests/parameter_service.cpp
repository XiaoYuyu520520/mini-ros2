// The six parameter services: what a tool on the other side of the wire sees.
//
// Until these existed the shim could `declare`, `get` and `set` its own
// parameters, and a change reached the code inside its own process
// (`detail::ParameterEventHub`) - but no other process could read or write a
// running node's parameters, because the six services `ros2 param` talks to
// were a stub. A parameter service that is *nearly* right is worse than one
// that is missing: `ros2 param set` answers "successful" and the node keeps the
// old value, or a set silently changes a parameter's type and the next
// `get_parameter<double>` throws somewhere else entirely. So the semantics the
// srv files describe are pinned here, one check group each:
//
//   1. all six are advertised, under the node's fully qualified name and with
//      the right type, which is what a client's `wait_for_service()` and
//      `ros2 service list -t` read,
//   2. every one of the nine `ParameterValue` types survives the round trip
//      through the message, including the two empty-array cases a
//      size-then-elements serialisation can lose,
//   3. `get_parameters` and `get_parameter_types` answer in request order and
//      say `PARAMETER_NOT_SET` for a name that is not set,
//   4. `set_parameters` applies what it can and reports per name, refusing a
//      name the node does not have, a type change, and a `read_only`
//      parameter,
//   5. `set_parameters_atomically` applies all of it or none of it,
//   6. `describe_parameters` reports the declared type, description and
//      read-only flag,
//   7. `list_parameters` lists what is under the requested prefixes and hands
//      back the groups that are deeper than the requested depth,
//   8. `NodeOptions::start_parameter_services(false)` really means none of it -
//      `nav2_ros_common::generate_internal_node()` builds every private helper
//      node that way, and a flag that is accepted and ignored puts twelve
//      channels on each of them,
//   9. a parameter service's channel is sized for a name list, not for a map
//      (twelve default-sized rings per node is 192 MB of committed pagefile).
#include "rclcpp/node.hpp"
#include "rclcpp/node_options.hpp"

#include "rcl_interfaces/srv/describe_parameters.hpp"
#include "rcl_interfaces/srv/get_parameter_types.hpp"
#include "rcl_interfaces/srv/get_parameters.hpp"
#include "rcl_interfaces/srv/list_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters_atomically.hpp"

#include "ros2_shim/graph.hpp"
#include "ros2_shim/parameter_support.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
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

rclcpp::NodeOptions optionsWith(const std::vector<std::string> & rules)
{
  std::vector<std::string> arguments{"--ros-args"};
  for (const std::string & rule : rules) {
    arguments.push_back("-r");
    arguments.push_back(rule);
  }
  rclcpp::NodeOptions options;
  options.arguments(arguments);
  return options;
}

// One client per service, held for as long as the test runs - not one per call.
//
// A node has one channel per name (`Node::get_or_create_channel`), and
// `Client::poll` reads that channel from a cursor *the channel* owns: two
// clients of the same service in one process therefore take turns reading each
// other's answers, and the older client - whose request has long been answered
// - consumes the newer one's reply and drops it. A tool creates one client and
// makes its calls through it; so does this test. (Across processes, which is
// where every real caller lives, each process has its own cursor and the
// question does not arise.)
template<class ServiceT>
class Caller
{
public:
  using Request = typename ServiceT::Request;
  using Response = typename ServiceT::Response;

  Caller(rclcpp::Node & node, const std::string & name)
  : node_(node), name_(name), client_(node.create_client<ServiceT>(name))
  {
    // The service is advertised in the shim graph by its constructor, which is
    // what a client's `wait_for_service()` reads.
    expect(client_->wait_for_service(std::chrono::milliseconds(500)), name_ + " is advertised");
  }

  bool call(const Request & request, Response & response)
  {
    auto future = client_->async_send_request(std::make_shared<Request>(request));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      node_.spin_once();
      if (std::chrono::steady_clock::now() > deadline) {
        std::printf("  FAIL %s did not answer\n", name_.c_str());
        ++failures;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    response = *future.get();
    return true;
  }

private:
  rclcpp::Node & node_;
  std::string name_;
  typename rclcpp::Client<ServiceT>::SharedPtr client_;
};

rcl_interfaces::msg::Parameter parameterMessage(
  const std::string & name, const rclcpp::ParameterValue & value)
{
  return ros2_shim::to_message(rclcpp::Parameter(name, value));
}

bool contains(const std::vector<std::string> & values, const std::string & wanted)
{
  for (const std::string & value : values) {
    if (value == wanted) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main()
{
  // The services are advertised under the node's fully qualified name, with the
  // type a tool resolves before it can call them: that is what makes
  // `ros2 service call /param_node/get_parameters
  // rcl_interfaces/srv/GetParameters` possible without knowing the node.
  {
    rclcpp::Node node("param_node");
    const char * suffixes[] = {
      "get_parameters", "get_parameter_types", "set_parameters",
      "set_parameters_atomically", "describe_parameters", "list_parameters"};
    for (const char * suffix : suffixes) {
      const std::string name = "/param_node/" + std::string(suffix);
      expect(
        ros2_shim::graph::service_exists(name),
        std::string("the node advertises ") + name);
    }

    std::string get_parameters_type;
    for (const auto & entry : ros2_shim::graph::list_services()) {
      if (entry.name == "/param_node/get_parameters") {
        get_parameters_type = entry.type;
      }
    }
    expect(
      get_parameters_type == "rcl_interfaces/srv/GetParameters",
      "get_parameters is advertised as rcl_interfaces/srv/GetParameters, not '" +
      get_parameters_type + "'");
  }

  // A namespaced node answers under its namespace: `-r __ns:=` is how nav2 puts
  // a costmap node where the lifecycle manager expects to find it.
  {
    rclcpp::Node node(
      "costmap", optionsWith({"__ns:=global_costmap", "__node:=global_costmap"}));
    expect(
      ros2_shim::graph::service_exists("/global_costmap/global_costmap/get_parameters"),
      "a namespaced node's parameter service carries the namespace");
  }

  // The opt-out is real: `generate_internal_node()` builds nav2's private nodes
  // with it.
  {
    rclcpp::Node quiet("quiet_node", rclcpp::NodeOptions().start_parameter_services(false));
    expect(
      !ros2_shim::graph::service_exists("/quiet_node/get_parameters"),
      "start_parameter_services(false) advertises nothing");
  }

  rclcpp::Node node("param_node");
  Caller<rcl_interfaces::srv::GetParameters> get(node, "/param_node/get_parameters");
  Caller<rcl_interfaces::srv::GetParameterTypes> types(node, "/param_node/get_parameter_types");
  Caller<rcl_interfaces::srv::SetParameters> set(node, "/param_node/set_parameters");
  Caller<rcl_interfaces::srv::SetParametersAtomically> set_atomically(
    node, "/param_node/set_parameters_atomically");
  Caller<rcl_interfaces::srv::DescribeParameters> describe(
    node, "/param_node/describe_parameters");
  Caller<rcl_interfaces::srv::ListParameters> list(node, "/param_node/list_parameters");

  rcl_interfaces::msg::ParameterDescriptor described;
  described.description = "how fast it goes";
  node.declare_parameter("a_bool", true);
  node.declare_parameter("an_int", 7);
  node.declare_parameter("a_double", 0.5, described);
  node.declare_parameter("a_string", std::string("hello"));
  node.declare_parameter("byte_array", std::vector<uint8_t>{1, 2, 255});
  node.declare_parameter("bool_array", std::vector<bool>{true, false});
  node.declare_parameter("int_array", std::vector<int64_t>{1, 2, 3});
  node.declare_parameter("double_array", std::vector<double>{0.5, 2.0});
  node.declare_parameter("string_array", std::vector<std::string>{"a", "b"});
  node.declare_parameter("empty_int_array", std::vector<int64_t>{});
  node.declare_parameter("empty_string", std::string(""));
  rcl_interfaces::msg::ParameterDescriptor frozen;
  frozen.description = "not for you";
  frozen.read_only = true;
  node.declare_parameter("frozen", 3, frozen);
  // Dotted names, for the depth half of `list_parameters`.
  node.declare_parameter("costmap.resolution", 0.05);
  node.declare_parameter("costmap.plugin", std::string("voxel_layer"));
  node.declare_parameter("costmap.layer.inscribed", 0.1);

  // 2. every type, in request order
  {
    rcl_interfaces::srv::GetParameters::Request request;
    request.names = {
      "a_bool", "an_int", "a_double", "a_string", "byte_array", "bool_array",
      "int_array", "double_array", "string_array", "empty_int_array", "empty_string"};
    rcl_interfaces::srv::GetParameters::Response response;
    if (get.call(request, response)) {
      const auto & v = response.values;
      expect(v.size() == request.names.size(), "one value per requested name");
      if (v.size() == request.names.size()) {
        expect(v[0].type == 1 && v[0].bool_value, "a bool comes back as a bool");
        expect(v[1].type == 2 && v[1].integer_value == 7, "an integer comes back as an integer");
        expect(v[2].type == 3 && v[2].double_value == 0.5, "a double comes back as a double");
        expect(v[3].type == 4 && v[3].string_value == "hello", "a string comes back as a string");
        expect(v[4].type == 5 && v[4].byte_array_value == std::vector<uint8_t>({1, 2, 255}),
          "a byte array comes back element for element");
        expect(v[5].type == 6 && v[5].bool_array_value == std::vector<bool>({true, false}),
          "a bool array comes back element for element");
        expect(v[6].type == 7 && v[6].integer_array_value == std::vector<int64_t>({1, 2, 3}),
          "an integer array comes back element for element");
        expect(v[7].type == 8 && v[7].double_array_value == std::vector<double>({0.5, 2.0}),
          "a double array comes back element for element");
        expect(
          v[8].type == 9 && v[8].string_array_value == std::vector<std::string>({"a", "b"}),
          "a string array comes back element for element");
        expect(v[9].type == 7 && v[9].integer_array_value.empty(),
          "an empty integer array is still an integer array");
        // A declared empty string is a value, not a parameter that was never
        // set: only `PARAMETER_NOT_SET` means "not set".
        expect(v[10].type == 4 && v[10].string_value.empty(), "an empty string is set");
      }
    }
  }

  // 3. order, and names nobody declared
  {
    rcl_interfaces::srv::GetParameters::Request request;
    request.names = {"a_bool", "nobody_declared_this", "a_double"};
    rcl_interfaces::srv::GetParameters::Response response;
    if (get.call(request, response)) {
      expect(response.values.size() == 3, "one value per requested name, declared or not");
      if (response.values.size() == 3) {
        expect(response.values[1].type == 0, "an undeclared name answers PARAMETER_NOT_SET");
        expect(response.values[2].type == 3, "and the names after it are still in order");
      }
    }

    rcl_interfaces::srv::GetParameterTypes::Request types_request;
    types_request.names = {"a_double", "nobody_declared_this", "string_array"};
    rcl_interfaces::srv::GetParameterTypes::Response types_response;
    if (types.call(types_request, types_response)) {
      expect(
        types_response.types == std::vector<uint8_t>({3, 0, 9}),
        "get_parameter_types answers one type per name");
    }
  }

  // 4. set_parameters: per name, and only what is there
  {
    rcl_interfaces::srv::SetParameters::Request request;
    request.parameters.push_back(parameterMessage("a_double", rclcpp::ParameterValue(1.5)));
    rcl_interfaces::srv::SetParameters::Response response;
    if (set.call(request, response)) {
      expect(response.results.size() == 1 && response.results[0].successful,
        "a declared parameter can be set");
      expect(node.get_parameter("a_double").get_value().get<double>() == 1.5,
        "and the new value is the node's");
    }

    request.parameters.clear();
    request.parameters.push_back(parameterMessage("an_int", rclcpp::ParameterValue(int64_t{9})));
    request.parameters.push_back(parameterMessage("nobody_declared_this", rclcpp::ParameterValue(1)));
    if (set.call(request, response)) {
      expect(response.results.size() == 2, "one result per requested parameter");
      if (response.results.size() == 2) {
        expect(response.results[0].successful, "the declared one is set");
        expect(!response.results[1].successful, "the undeclared one is refused");
        expect(
          response.results[1].reason.find("has not been declared") != std::string::npos,
          "and the reason says so: '" + response.results[1].reason + "'");
      }
      expect(node.get_parameter("an_int").get_value().get<int64_t>() == 9,
        "a refusal next to it does not stop the set");
      expect(!node.has_parameter("nobody_declared_this"),
        "a service call cannot create a parameter");
    }

    request.parameters.clear();
    request.parameters.push_back(parameterMessage("a_double", rclcpp::ParameterValue("1.5")));
    if (set.call(request, response)) {
      expect(response.results.size() == 1 && !response.results[0].successful,
        "a string is not a double");
      expect(
        response.results[0].reason.find("Double") != std::string::npos,
        "and the reason names both types: '" + response.results[0].reason + "'");
      expect(node.get_parameter("a_double").get_value().get<double>() == 1.5,
        "the old value survives a refused type change");
    }

    request.parameters.clear();
    request.parameters.push_back(parameterMessage("frozen", rclcpp::ParameterValue(int64_t{4})));
    if (set.call(request, response)) {
      expect(response.results.size() == 1 && !response.results[0].successful,
        "a read_only parameter is refused");
      expect(node.get_parameter("frozen").get_value().get<int64_t>() == 3,
        "and keeps its value");
    }
  }

  // 5. atomically: all of it, or none of it
  {
    rcl_interfaces::srv::SetParametersAtomically::Request request;
    request.parameters.push_back(parameterMessage("an_int", rclcpp::ParameterValue(int64_t{11})));
    request.parameters.push_back(
      parameterMessage("a_string", rclcpp::ParameterValue("changed")));
    rcl_interfaces::srv::SetParametersAtomically::Response response;
    if (set_atomically.call(request, response)) {
      expect(response.result.successful, "an atomic set of two good values succeeds");
      expect(node.get_parameter("an_int").get_value().get<int64_t>() == 11 &&
        node.get_parameter("a_string").get_value().get<std::string>() == "changed",
        "and both values are the node's");
    }

    request.parameters.clear();
    request.parameters.push_back(parameterMessage("an_int", rclcpp::ParameterValue(int64_t{12})));
    request.parameters.push_back(
      parameterMessage("nobody_declared_this", rclcpp::ParameterValue(1)));
    if (set_atomically.call(request, response)) {
      expect(!response.result.successful, "one bad name fails the whole atomic set");
      expect(node.get_parameter("an_int").get_value().get<int64_t>() == 11,
        "and the good half of it was not applied");
    }
  }

  // 6. describe
  {
    rcl_interfaces::srv::DescribeParameters::Request request;
    request.names = {"a_double", "frozen", "nobody_declared_this"};
    rcl_interfaces::srv::DescribeParameters::Response response;
    if (describe.call(request, response)) {
      expect(response.descriptors.size() == 3, "one descriptor per requested name");
      if (response.descriptors.size() == 3) {
        const auto & described_double = response.descriptors[0];
        expect(described_double.name == "a_double", "the descriptor carries the name");
        expect(described_double.type == 3, "and the type it was declared with");
        expect(described_double.description == "how fast it goes",
          "and what the declaring code said about it");
        expect(!described_double.read_only, "and that it is writable");
        expect(response.descriptors[1].read_only, "a read_only declaration is reported");
        expect(response.descriptors[2].type == 0,
          "an undeclared name answers PARAMETER_NOT_SET rather than failing the call");
        expect(response.descriptors[2].name == "nobody_declared_this",
          "with the name that was asked about");
      }
    }
    // A parameter declared without a descriptor still reports its type: that is
    // the difference between "nothing to print" and the truth.
    rcl_interfaces::srv::DescribeParameters::Request plain_request;
    plain_request.names = {"a_string"};
    rcl_interfaces::srv::DescribeParameters::Response plain_response;
    if (describe.call(plain_request, plain_response)) {
      expect(
        plain_response.descriptors.size() == 1 && plain_response.descriptors[0].type == 4,
        "a parameter declared without a descriptor reports the type of its value");
    }
  }

  // 7. list, with prefixes and depth
  {
    rcl_interfaces::srv::ListParameters::Request request;
    request.depth = rcl_interfaces::srv::ListParameters::Request::DEPTH_RECURSIVE;
    rcl_interfaces::srv::ListParameters::Response response;
    if (list.call(request, response)) {
      const auto & names = response.result.names;
      expect(contains(names, "use_sim_time"), "a recursive listing includes use_sim_time");
      expect(contains(names, "costmap.layer.inscribed"),
        "and everything under a dotted name");
      expect(response.result.prefixes.empty(), "a recursive listing has no groups left over");
      expect(names.size() == node.parameters_snapshot().size(),
        "and lists every parameter once");
    }

    request.prefixes.clear();
    request.depth = 1;
    if (list.call(request, response)) {
      expect(contains(response.result.names, "use_sim_time"),
        "a depth of 1 lists a name with nothing under it");
      expect(!contains(response.result.names, "costmap.resolution"),
        "and not one that has a group under it");
      expect(contains(response.result.prefixes, "costmap."),
        "the group comes back as a prefix, separator included");
    }

    request.prefixes = {"costmap."};
    request.depth = 1;
    if (list.call(request, response)) {
      expect(contains(response.result.names, "costmap.resolution"),
        "under a prefix, depth counts from the prefix");
      expect(contains(response.result.names, "costmap.plugin"), "both direct children");
      expect(!contains(response.result.names, "costmap.layer.inscribed"), "and not deeper");
      expect(contains(response.result.prefixes, "costmap.layer."),
        "the deeper group is reported under the prefix it was asked about");
      expect(!contains(response.result.prefixes, "costmap."),
        "and the prefix itself is not one of them");
    }

    request.prefixes = {"costmap.", "a_"};
    request.depth = 1;
    if (list.call(request, response)) {
      expect(contains(response.result.names, "a_bool") &&
        contains(response.result.names, "costmap.plugin"),
        "two prefixes list both sets");
      expect(!contains(response.result.names, "an_int"),
        "and nothing outside them");
    }
  }

  // 9. a parameter service's channel is sized for a name list
  {
    auto channel = node.get_channel("/param_node/get_parameters/_response");
    expect(channel != nullptr, "the response channel exists");
    if (channel) {
      expect(
        channel->slot_size() == rclcpp::shim::parameter_slot_size(),
        "and is a parameter-sized slot, not the default one");
      expect(
        channel->slot_size() < rclcpp::shim::default_slot_size(),
        "which is what keeps twelve channels per node off the pagefile");
    }
  }

  if (failures != 0) {
    std::printf("parameter_service: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("parameter_service: all checks passed\n");
  return 0;
}
