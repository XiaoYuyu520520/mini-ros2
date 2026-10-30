// A `ros2`-style command line front end for the Windows shared-memory shim.
//
//   ros2 topic list [-t] [-v]
//   ros2 topic info <topic>
//   ros2 topic type <topic>
//   ros2 topic echo <topic> [--count N] [--timeout S]
//   ros2 topic hz <topic> [--window N]
//   ros2 topic pub <topic> <type> [values] [--once|-1] [--rate|-r HZ]
//                                  [--times|-t N] [--print|-p] [--duration S]
//   ros2 topic find <regex>
//   ros2 service list [-t] [-v]
//   ros2 service type <service>
//   ros2 service find <regex>
//   ros2 service call <service> <type> [values] [--timeout S]
//   ros2 action list [-t] [-v]
//   ros2 action type <action>
//   ros2 action find <regex>
//   ros2 action send_goal <action> <type> [values] [--timeout|-t S] [--feedback|-f]
//   ros2 action cancel <action> <goal id>
//   ros2 param list [node] [-t]
//   ros2 param get <node> <name>
//   ros2 param set <node> <name> <value>
//   ros2 param describe <node> <name>
//   ros2 param dump <node>
//   ros2 node list
//   ros2 interface list
//   ros2 bridge status
//
// It reads the shim graph registry, so it sees local nodes as well as topics,
// services and actions mirrored in from the remote robot by
// `ros2_bridge_daemon`.
//
// `values` is what real ROS2 takes there: one YAML document, e.g.
// `"{linear: {x: 0.5}}"` or `"{data: Hello World}"`, which
// ros2_bridge/yaml_assign.hpp flattens onto the generated message fields. The
// flat spelling this tree started with - `field.path=value`, e.g.
// `linear.x=0.5 data=hi` - keeps working, and the two can be mixed in one
// command.
//
// `service call` is a client of the shim's own service protocol (see
// ros2_shim/include/rclcpp/client.hpp): it writes the request - a correlation
// id in front of the shim layout of the message - onto `<name>/_request` and
// waits for its own id to come back on `<name>/_response`. Whether the
// answering process is a node on this machine or the bridge daemon forwarding
// to the robot makes no difference here.
//
// `action send_goal` and `action cancel` are clients of the shim's action
// protocol in the same way (see ros2_shim/include/rclcpp_action/
// rclcpp_action.hpp): the goal goes out on `<name>/_goal` with a goal id in
// front of it, the answer comes back on `<name>/_goal_ack`, and feedback and
// the result follow on `<name>/_feedback` and `<name>/_result`.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "rcl_interfaces/srv/describe_parameters.hpp"
#include "rcl_interfaces/srv/get_parameter_types.hpp"
#include "rcl_interfaces/srv/get_parameters.hpp"
#include "rcl_interfaces/srv/list_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters.hpp"
#include "rcl_interfaces/srv/set_parameters_atomically.hpp"

#include "rclcpp/precise_wait.hpp"

#include "ros2_bridge/action_registry.hpp"
#include "ros2_bridge/service_registry.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/type_registry.hpp"
#include "ros2_bridge/yaml_assign.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/parameter_support.hpp"
#include "ros2_shim/parameter_text.hpp"
#include "ros2_shim/serialization.hpp"

namespace
{

using ros2_bridge::Bytes;
using ros2_bridge::FieldAssign;

// Spelled out once here; the three value-reading subcommands below all use it.
namespace yaml_assign = ros2_bridge::yaml_assign;

std::atomic<bool> g_running{true};

void handleSignal(int)
{
  g_running.store(false);
}

struct TopicRow
{
  std::string name;
  std::string type;
  int publishers = 0;
  int subscribers = 0;
  std::vector<std::string> sources;
  bool remote = false;
};

std::vector<TopicRow> collectTopics()
{
  std::map<std::string, TopicRow> rows;
  for (const auto & entry : ros2_shim::graph::list_topics()) {
    TopicRow & row = rows[entry.name];
    row.name = entry.name;
    if (row.type.empty() || entry.source == "local") {
      row.type = entry.type;
    }
    row.publishers += entry.publishers;
    row.subscribers += entry.subscribers;
    row.sources.push_back(entry.source);
    if (entry.source.rfind("remote:", 0) == 0) {
      row.remote = true;
    }
  }
  std::vector<TopicRow> out;
  out.reserve(rows.size());
  for (auto & pair : rows) {
    out.push_back(pair.second);
  }
  return out;
}

struct ServiceRow
{
  std::string name;
  std::string type;
  std::vector<std::string> sources;
  bool remote = false;
};

std::vector<ServiceRow> collectServices()
{
  std::map<std::string, ServiceRow> rows;
  for (const auto & entry : ros2_shim::graph::list_services()) {
    ServiceRow & row = rows[entry.name];
    row.name = entry.name;
    if (row.type.empty() || entry.source == "local") {
      row.type = entry.type;
    }
    row.sources.push_back(entry.source);
    if (entry.source.rfind("remote:", 0) == 0) {
      row.remote = true;
    }
  }
  std::vector<ServiceRow> out;
  out.reserve(rows.size());
  for (auto & pair : rows) {
    out.push_back(pair.second);
  }
  return out;
}

struct ActionRow
{
  std::string name;
  std::string type;
  std::vector<std::string> sources;
  bool remote = false;
};

std::vector<ActionRow> collectActions()
{
  std::map<std::string, ActionRow> rows;
  for (const auto & entry : ros2_shim::graph::list_actions()) {
    ActionRow & row = rows[entry.name];
    row.name = entry.name;
    if (row.type.empty() || entry.source == "local") {
      row.type = entry.type;
    }
    row.sources.push_back(entry.source);
    if (entry.source.rfind("remote:", 0) == 0) {
      row.remote = true;
    }
  }
  std::vector<ActionRow> out;
  out.reserve(rows.size());
  for (auto & pair : rows) {
    out.push_back(pair.second);
  }
  return out;
}

const ActionRow * findAction(const std::vector<ActionRow> & actions, const std::string & name);

// The name of the byte in front of a result payload, i.e. the shim's
// `rclcpp_action::ResultCode`.
const char * resultCodeName(std::uint8_t code)
{
  switch (code) {
    case 1: return "SUCCEEDED";
    case 2: return "ABORTED";
    case 3: return "CANCELED";
    default: return "UNKNOWN";
  }
}

// `std_srvs/Trigger` and `/sim/status` both have to land on the same row.
std::string qualifyName(const std::string & name)
{
  std::string wanted = name;
  if (!wanted.empty() && wanted.front() != '/') {
    wanted.insert(wanted.begin(), '/');
  }
  return wanted;
}

const ServiceRow * findService(const std::vector<ServiceRow> & services, const std::string & name)
{
  const std::string wanted = qualifyName(name);
  for (const auto & service : services) {
    if (service.name == wanted) {
      return &service;
    }
  }
  return nullptr;
}

const ActionRow * findAction(const std::vector<ActionRow> & actions, const std::string & name)
{
  const std::string wanted = qualifyName(name);
  for (const auto & action : actions) {
    if (action.name == wanted) {
      return &action;
    }
  }
  return nullptr;
}

const TopicRow * findTopic(const std::vector<TopicRow> & topics, const std::string & name)
{
  std::string wanted = name;
  if (!wanted.empty() && wanted.front() != '/') {
    wanted.insert(wanted.begin(), '/');
  }
  for (const auto & topic : topics) {
    if (topic.name == wanted) {
      return &topic;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------- topic list

int cmdTopicList(int argc, char ** argv)
{
  bool show_types = false;
  bool verbose = false;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-t" || arg == "--show-types") {
      show_types = true;
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
      show_types = true;
    }
  }
  const auto topics = collectTopics();
  for (const auto & topic : topics) {
    if (show_types) {
      std::printf("%s [%s]\n", topic.name.c_str(), topic.type.empty() ? "?" : topic.type.c_str());
    } else {
      std::printf("%s\n", topic.name.c_str());
    }
    if (verbose) {
      std::printf(
        "    publishers=%d subscribers=%d source=%s\n", topic.publishers, topic.subscribers,
        topic.remote ? "remote+bridge" : "local");
    }
  }
  return 0;
}

int cmdTopicFind(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 topic find <regex>\n");
    return 1;
  }
  const std::regex pattern(argv[0]);
  for (const auto & topic : collectTopics()) {
    if (std::regex_search(topic.name, pattern)) {
      std::printf("%s\n", topic.name.c_str());
    }
  }
  return 0;
}

int cmdTopicInfo(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 topic info <topic>\n");
    return 1;
  }
  const auto topics = collectTopics();
  const TopicRow * topic = findTopic(topics, argv[0]);
  if (topic == nullptr) {
    std::fprintf(stderr, "Unknown topic '%s'\n", argv[0]);
    return 1;
  }
  std::printf("Type: %s\n\n", topic->type.c_str());
  std::printf("Publisher count: %d\n", topic->publishers);
  std::printf("Subscription count: %d\n", topic->subscribers);
  std::printf("Source: %s\n", topic->remote ? "remote (bridged over gRPC)" : "local (shared memory)");
  return 0;
}

int cmdTopicType(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 topic type <topic>\n");
    return 1;
  }
  const auto topics = collectTopics();
  const TopicRow * topic = findTopic(topics, argv[0]);
  if (topic == nullptr || topic->type.empty()) {
    std::fprintf(stderr, "Unknown topic '%s'\n", argv[0]);
    return 1;
  }
  std::printf("%s\n", topic->type.c_str());
  return 0;
}

// -------------------------------------------------------------------- echo

// `ros2 topic echo` / `hz` are readers, and a reader that does not say so in
// the graph is invisible to every publisher on the machine. Registered under a
// name of its own so `ros2 node list` shows who is listening, and removed by
// the guard's destructor so a Ctrl-C leaves no phantom subscriber behind - a
// stale one would keep the planner publishing plans to nobody.
static void registerSubscriber(const std::string & topic, const std::string & type)
{
  auto & registry = ros2_shim::graph::ProcessRegistry::instance();
  registry.add_node("/ros2_cli_sub");
  registry.add_topic(topic, type, "sub");
}

// Upgrades the type of an already-registered subscription once the publisher
// appears. `add_topic` is what has the slot, but calling it again would count a
// second subscriber - so the record is removed and re-added, which is the same
// thing `registerSubscriber` did the first time.
static void refreshSubscriberType(const std::string & topic, const std::string & type)
{
  auto & registry = ros2_shim::graph::ProcessRegistry::instance();
  registry.remove_topic(topic, "sub");
  registry.add_topic(topic, type, "sub");
}

struct CliSubscription
{
  std::string topic;
  ~CliSubscription()
  {
    auto & registry = ros2_shim::graph::ProcessRegistry::instance();
    registry.remove_topic(topic, "sub");
    registry.remove_node("/ros2_cli_sub");
  }
};

int cmdTopicEcho(int argc, char ** argv)
{
  std::string name;
  int count = 0;      // 0 = until interrupted
  double timeout_s = 0;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--once" || arg == "-1") {
      count = 1;
    } else if ((arg == "--count" || arg == "-n") && i + 1 < argc) {
      count = std::atoi(argv[++i]);
    } else if (arg == "--timeout" && i + 1 < argc) {
      timeout_s = std::atof(argv[++i]);
    } else if (!arg.empty() && arg[0] == '-') {
      // An unknown option used to be ignored, which turned the next argument
      // into the topic name and produced a confusing "channel does not exist"
      // error. Fail loudly instead.
      std::fprintf(stderr, "[ros2] unknown option '%s'\n", arg.c_str());
      std::fprintf(stderr, "usage: ros2 topic echo <topic> [--count N] [--once|-1] [--timeout S]\n");
      return 1;
    } else if (name.empty()) {
      name = arg;
    } else {
      std::fprintf(stderr, "[ros2] unexpected argument '%s'\n", arg.c_str());
      std::fprintf(stderr, "usage: ros2 topic echo <topic> [--count N] [--once|-1] [--timeout S]\n");
      return 1;
    }
  }
  if (name.empty()) {
    std::fprintf(stderr, "usage: ros2 topic echo <topic> [--count N] [--once|-1] [--timeout S]\n");
    return 1;
  }

  std::string error;
  // A subscription may legitimately arrive *before* the publisher - real ROS2
  // creates the reader and waits. Opening alone returns null while nobody has
  // created the channel yet, which made `ros2 topic echo` exit instantly on a
  // topic that was simply not published to yet. Creating it is the shim's own
  // rule for "two processes may both be first to touch a channel", and it is
  // what a subscriber is: the owner of its read end.
  auto channel = ros2_bridge::create_channel(name, &error);
  if (!channel) {
    std::fprintf(stderr, "[ros2] cannot open '%s': %s\n", name.c_str(), error.c_str());
    return 1;
  }

  std::string type;
  const auto topics = collectTopics();
  if (const TopicRow * row = findTopic(topics, name); row != nullptr) {
    type = row->type;
  }
  // A reader is a subscription, and on this port the graph *is* how a
  // publisher learns that somebody is listening: nav2's planner only puts a
  // plan on /plan and its controller only writes /cmd_vel when that count is
  // above zero. A CLI reader that skipped the record saw nothing at all, which
  // is what `ros2 topic echo /plan` did before this was added.
  registerSubscriber(name, type);
  const CliSubscription unregister{name};

  std::vector<std::uint8_t> buffer(channel->slot_size());
  const auto started = std::chrono::steady_clock::now();
  int seen = 0;
  // The type may be unknown because the publisher has not appeared yet. It is
  // looked up again while waiting, so a reader that started first still
  // decodes the messages once a publisher registers the topic - the same
  // "subscribe now, publish later" ordering real ROS2 allows.
  auto next_type_check = started;
  while (g_running.load()) {
    if (timeout_s > 0 &&
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > timeout_s)
    {
      break;
    }
    if (type.empty()) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_type_check) {
        if (const TopicRow * row = findTopic(collectTopics(), name); row != nullptr &&
          !row->type.empty())
        {
          type = row->type;
          refreshSubscriberType(name, type);
        }
        next_type_check = now + std::chrono::seconds(1);
      }
    }
    std::size_t size = 0;
    if (!channel->subscribe(buffer.data(), buffer.size(), &size, 200)) {
      continue;
    }
    Bytes shim(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
    const std::string text = ros2_bridge::describe_shim(type, shim);
    if (text.empty() && ros2_bridge::is_supported(type)) {
      // Nothing to print is not the same as nothing to decode: a converted
      // `std_msgs/msg/Empty` is an empty line and a separator, the way the real
      // CLI shows it. Only a type with no codec falls back to a byte count.
      std::printf("---\n");
    } else if (text.empty()) {
      std::printf("(%zu bytes of %s)\n", shim.size(), type.empty() ? "unknown type" : type.c_str());
    } else {
      std::printf("%s---\n", text.c_str());
    }
    std::fflush(stdout);
    if (count > 0 && ++seen >= count) {
      break;
    }
  }
  return 0;
}

int cmdTopicHz(int argc, char ** argv)
{
  std::string name;
  int window = 20;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--window" && i + 1 < argc) {
      window = std::atoi(argv[++i]);
    } else if (!arg.empty() && arg[0] != '-') {
      name = arg;
    }
  }
  if (name.empty()) {
    std::fprintf(stderr, "usage: ros2 topic hz <topic> [--window N]\n");
    return 1;
  }
  std::string error;
  // Same rule as `topic echo`: a rate measurement may start before the
  // publisher exists, and must keep waiting rather than exit. Creating the
  // channel (rather than only opening it) is what lets the reader attach to
  // a topic nobody has published to yet.
  auto channel = ros2_bridge::create_channel(name, &error);
  if (!channel) {
    std::fprintf(stderr, "[ros2] cannot open '%s': %s\n", name.c_str(), error.c_str());
    return 1;
  }

  std::string type;
  const auto known = collectTopics();
  if (const TopicRow * row = findTopic(known, name); row != nullptr) {
    type = row->type;
  }
  // A reader is a subscription, and on this port the graph *is* how a
  // publisher learns that somebody is listening: nav2's planner only puts a
  // plan on /plan and its controller only writes /cmd_vel when that count is
  // above zero. A CLI reader that skipped the record saw nothing at all, which
  // is what `ros2 topic echo /plan` did before this was added.
  registerSubscriber(name, type);
  const CliSubscription unregister{name};

  std::vector<std::uint8_t> buffer(channel->slot_size());
  std::vector<double> intervals;

  // A reader that just attached still sees the messages of the ring buffer, so
  // drain those first: measuring them would report a bogus rate for the first
  // few windows (the very first interval used to be ~0 seconds).
  {
    std::size_t ignored = 0;
    while (channel->subscribe(buffer.data(), buffer.size(), &ignored, 0)) {
    }
  }

  auto last = std::chrono::steady_clock::now();
  bool first = true;
  std::printf("subscribed to [%s]\n", name.c_str());
  while (g_running.load()) {
    std::size_t size = 0;
    if (!channel->subscribe(buffer.data(), buffer.size(), &size, 500)) {
      continue;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!first) {
      intervals.push_back(std::chrono::duration<double>(now - last).count());
      if (static_cast<int>(intervals.size()) > window) {
        intervals.erase(intervals.begin());
      }
    }
    first = false;
    last = now;
    if (!intervals.empty()) {
      double sum = 0;
      double minimum = intervals.front();
      double maximum = intervals.front();
      for (double value : intervals) {
        sum += value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
      }
      const double average = sum / static_cast<double>(intervals.size());
      std::printf(
        "average rate: %.3f\n\tmin: %.3f max: %.3f std dev: %.3f window: %zu\n",
        1.0 / average, 1.0 / maximum, 1.0 / minimum, 0.0, intervals.size());
      std::fflush(stdout);
    }
  }
  return 0;
}

// The message values of `topic pub`, `service call` and `action send_goal`.
//
// Real ROS2 reads them as YAML and this tree started with `field.path=value`
// assignments; both spellings are accepted and both end up as the same flat
// list of assignments (ros2_bridge/yaml_assign.hpp does the flattening, so
// `{linear: {x: 0.5}}` and `linear.x=0.5` reach the codec as the same thing).
// A value that cannot be represented - a sequence of mappings, an unterminated
// brace, a field name missing in front of an `=` - is an error, never a
// silently different message.

// Is this argument a message value rather than the topic name or the type?
bool isValueArgument(const std::string & arg)
{
  const yaml_assign::Form form = yaml_assign::classify(arg);
  return form == yaml_assign::Form::Flat || form == yaml_assign::Form::Document;
}

// Reads the values starting at `index`, moving it over the rest of a YAML
// document a shell split into several arguments (`{linear: {x: 0.5}}` is one
// argument when it is quoted, three when the braces are expanded). False and a
// reason in `error` when the value is malformed.
bool readValueArguments(
  int argc, char ** argv, int & index, std::vector<FieldAssign> & fields, std::string & error)
{
  const std::string arg = argv[index];
  if (yaml_assign::classify(arg) == yaml_assign::Form::Document) {
    std::string document = arg;
    while (yaml_assign::openDepth(document) > 0 && index + 1 < argc) {
      document += " ";
      document += argv[++index];
    }
    if (!yaml_assign::parseDocument(document, fields, error)) {
      error = "'" + document + "': " + error;
      return false;
    }
    return true;
  }
  if (!yaml_assign::parseFlatAssignment(arg, fields, error)) {
    error = "'" + arg + "': " + error;
    return false;
  }
  return true;
}

// --------------------------------------------------------------------- pub

int cmdTopicPub(int argc, char ** argv)
{
  // Printed by every exit below, so the option list cannot drift apart.
  const char * const usage =
    "usage: ros2 topic pub <topic> <type> [values] [--once|-1] [--rate|-r HZ] [--times|-t N]"
    " [--print|-p] [--duration S]\n"
    "       values is one YAML document - '{linear: {x: 0.5}}', '{data: Hello World}', or the\n"
    "       block form 'linear:' then indented 'x: 0.5' - or field.path=value, 'linear.x=0.5'\n";

  std::string name;
  std::string type;
  std::vector<FieldAssign> fields;
  std::string error;
  bool once = false;
  bool print_message = false;
  double rate = 1.0;
  double duration = 0;
  long times = 0;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--once" || arg == "-1") {
      once = true;
    } else if ((arg == "--rate" || arg == "-r") && i + 1 < argc) {
      rate = std::atof(argv[++i]);
    } else if ((arg == "--times" || arg == "-t") && i + 1 < argc) {
      times = std::atol(argv[++i]);
      if (times <= 0) {
        std::fprintf(stderr, "[ros2] --times needs a count of 1 or more\n");
        return 1;
      }
    } else if (arg == "--print" || arg == "-p") {
      print_message = true;
    } else if (arg == "--duration" && i + 1 < argc) {
      duration = std::atof(argv[++i]);
    } else if (!arg.empty() && arg.front() == '-' && arg != "-") {
      // An unknown option used to be read as the topic name or the type, and
      // the command failed somewhere else entirely.
      std::fprintf(stderr, "[ros2] unknown option '%s'\n", arg.c_str());
      std::fputs(usage, stderr);
      return 1;
    } else if (isValueArgument(arg)) {
      if (!readValueArguments(argc, argv, i, fields, error)) {
        std::fprintf(stderr, "[ros2] cannot read the value: %s\n", error.c_str());
        std::fputs(usage, stderr);
        return 1;
      }
    } else if (name.empty()) {
      name = arg;
    } else if (type.empty()) {
      type = arg;
    } else {
      // Silently ignoring extra arguments hides typos in field assignments.
      std::fprintf(stderr, "[ros2] unexpected argument '%s'\n", arg.c_str());
      std::fputs(usage, stderr);
      return 1;
    }
  }
  if (name.empty() || type.empty()) {
    std::fputs(usage, stderr);
    return 1;
  }

  Bytes shim;
  if (!ros2_bridge::build_shim(type, fields, shim, &error)) {
    std::fprintf(stderr, "[ros2] cannot build %s: %s\n", type.c_str(), error.c_str());
    return 1;
  }

  // A channel nobody holds dies with this process: the `Local\` mapping is
  // created here and goes away on exit. When nobody was holding it already, a
  // single publish can therefore be missed - the bridge says nothing about it,
  // so say it here rather than printing "published 1 message" and stopping
  // there.
  const bool held_before = ros2_bridge::open_channel(name, &error) != nullptr;
  auto channel = ros2_bridge::create_channel(name, &error);
  if (!channel) {
    std::fprintf(stderr, "[ros2] cannot create '%s': %s\n", name.c_str(), error.c_str());
    return 1;
  }
  if (!held_before) {
    std::fprintf(
      stderr,
      "[ros2] warning: nobody was holding '%s' yet, so a --once publish may be missed "
      "(start the bridge daemon or a subscriber first)\n",
      name.c_str());
  }

  // Advertise the topic so the graph (and the bridge uplink) sees it.
  auto & registry = ros2_shim::graph::ProcessRegistry::instance();
  registry.add_node("/ros2_cli_pub");
  registry.add_topic(name, type, "pub");

  std::printf("publisher: beginning loop\n");
  const auto started = std::chrono::steady_clock::now();
  // `--once` is one message, `--times N` is N of them, neither is "until
  // interrupted" - the same three cases the real `ros2 topic pub` has.
  const long target = once ? 1 : times;

  // Publish on a *schedule*, not on "sleep(period)".
  //
  // The naive `sleep_for(1000/rate ms)` is only as precise as the OS timer
  // tick. On Windows that tick is 15.6 ms, so `-r 1000` (a 1 ms sleep) became
  // ~64 Hz - which is exactly the "1000 Hz published, 60 Hz received" that
  // showed up on a real machine. The deadline is advanced by the period and
  // the wait only sleeps the part it can sleep accurately, so the *average*
  // rate is the requested one even when a single interval is coarse.
  const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(1.0 / std::max(0.1, rate)));
  auto next = started;
  long published = 0;
  while (g_running.load()) {
    if (!channel->publish(shim.data(), shim.size())) {
      std::fprintf(stderr, "[ros2] publish failed (message larger than the slot?)\n");
      return 1;
    }
    ++published;
    if (print_message) {
      // What `ros2 topic echo` would print, made without waiting for anyone:
      // `-p` is how you see the effect of a value while writing it.
      std::printf(
        "publishing #%ld: %s", published, ros2_bridge::describe_shim(type, shim).c_str());
    }
    if (target > 0 && published >= target) {
      std::printf(
        "published %ld message%s on %s\n", published, published == 1 ? "" : "s", name.c_str());
      break;
    }
    if (duration > 0 &&
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >= duration)
    {
      break;
    }
    next += period;
    const auto now = std::chrono::steady_clock::now();
    if (next < now - period) {
      // Fell more than a whole period behind (a long pause, a debugger, a
      // loaded machine): resync instead of bursting messages to catch up. A
      // burst would look like a rate spike to `ros2 topic hz`.
      next = now;
    }
    rclcpp::shim::wait_until(next);
  }

  registry.remove_topic(name, "pub");
  registry.remove_node("/ros2_cli_pub");
  return 0;
}

// ---------------------------------------------------------------- services

int cmdServiceList(int argc, char ** argv)
{
  bool show_types = false;
  bool verbose = false;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-t" || arg == "--show-types") {
      show_types = true;
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
      show_types = true;
    }
  }
  for (const auto & service : collectServices()) {
    if (show_types) {
      std::printf(
        "%s [%s]\n", service.name.c_str(), service.type.empty() ? "?" : service.type.c_str());
    } else {
      std::printf("%s\n", service.name.c_str());
    }
    if (verbose) {
      std::printf(
        "    source=%s\n", service.remote ? "remote (bridged over gRPC)" : "local");
    }
  }
  return 0;
}

int cmdServiceType(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 service type <service>\n");
    return 1;
  }
  const auto services = collectServices();
  const ServiceRow * service = findService(services, argv[0]);
  if (service == nullptr || service->type.empty()) {
    std::fprintf(stderr, "Unknown service '%s'\n", argv[0]);
    return 1;
  }
  std::printf("%s\n", service->type.c_str());
  return 0;
}

int cmdServiceFind(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 service find <regex>\n");
    return 1;
  }
  const std::regex pattern(argv[0]);
  for (const auto & service : collectServices()) {
    if (std::regex_search(service.name, pattern)) {
      std::printf("%s\n", service.name.c_str());
    }
  }
  return 0;
}

int cmdServiceCall(int argc, char ** argv)
{
  const char * const usage =
    "usage: ros2 service call <service> <type> [values] [--timeout S]\n"
    "       values is one YAML document - '{data: true}', '{a: 1, b: 2}', or block 'data:' then\n"
    "       an indented 'true' - or field.path=value assignments, e.g. 'data=true'\n";

  std::string name;
  std::string type;
  std::vector<FieldAssign> fields;
  std::string error;
  double timeout = 5.0;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--timeout" && i + 1 < argc) {
      timeout = std::atof(argv[++i]);
    } else if (!arg.empty() && arg.front() == '-' && arg != "-") {
      std::fprintf(stderr, "[ros2] unknown option '%s'\n", arg.c_str());
      std::fputs(usage, stderr);
      return 1;
    } else if (isValueArgument(arg)) {
      if (!readValueArguments(argc, argv, i, fields, error)) {
        std::fprintf(stderr, "[ros2] cannot read the value: %s\n", error.c_str());
        std::fputs(usage, stderr);
        return 1;
      }
    } else if (name.empty()) {
      name = arg;
    } else if (type.empty()) {
      type = arg;
    } else {
      std::fprintf(stderr, "[ros2] unexpected argument '%s'\n", arg.c_str());
      std::fputs(usage, stderr);
      return 1;
    }
  }
  if (name.empty() || type.empty()) {
    std::fputs(usage, stderr);
    return 1;
  }
  name = qualifyName(name);

  // Asked for by name first, the way `ros2 service type` resolves it: the
  // caller may leave the type out in the future, and the graph is the only
  // place that knows.
  const auto services = collectServices();
  const ServiceRow * row = findService(services, name);
  if (row == nullptr) {
    std::fprintf(
      stderr,
      "[ros2] unknown service '%s' (is the bridge daemon up? `ros2 service list` shows what "
      "is reachable)\n", name.c_str());
    return 1;
  }
  if (row->type != type) {
    std::fprintf(
      stderr, "[ros2] warning: %s is advertised as %s, calling it as %s\n", name.c_str(),
      row->type.empty() ? "?" : row->type.c_str(), type.c_str());
  }

  const ros2_bridge::ServiceCodec * codec = ros2_bridge::find_service_codec(type);
  if (codec == nullptr) {
    std::fprintf(stderr, "[ros2] no codec for service type '%s'; known types:\n", type.c_str());
    for (const auto & known : ros2_bridge::supported_services()) {
      std::fprintf(stderr, "    %s\n", known.c_str());
    }
    return 1;
  }

  Bytes request;
  if (!codec->build_request(fields, request, error)) {
    std::fprintf(stderr, "[ros2] cannot build the request: %s\n", error.c_str());
    return 1;
  }
  std::printf("request:\n%s", codec->describe_request(request).c_str());

  auto requests = ros2_bridge::open_or_create_service_channel(name, "request", &error);
  if (!requests) {
    std::fprintf(stderr, "[ros2] cannot open '%s/_request': %s\n", name.c_str(), error.c_str());
    return 1;
  }
  auto responses = ros2_bridge::open_or_create_service_channel(name, "response", &error);
  if (!responses) {
    std::fprintf(stderr, "[ros2] cannot open '%s/_response': %s\n", name.c_str(), error.c_str());
    return 1;
  }

  // Start at the head of the answer ring: a `_response` channel keeps what an
  // earlier caller's answer put there (and its ids start at 1 again in every
  // new process), so reading from the tail would hand this call the previous
  // one's result.
  responses->seek_to_latest();

  const std::uint64_t id = 1;
  Bytes out(sizeof(id) + request.size());
  std::memcpy(out.data(), &id, sizeof(id));
  if (!request.empty()) {
    std::memcpy(out.data() + sizeof(id), request.data(), request.size());
  }
  if (!requests->publish(out.data(), out.size())) {
    std::fprintf(stderr, "[ros2] the request does not fit the '%s/_request' slot\n", name.c_str());
    return 1;
  }

  std::vector<std::uint8_t> buffer(responses->slot_size());
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(static_cast<long long>(std::max(0.0, timeout) * 1000.0));
  while (g_running.load()) {
    std::size_t size = 0;
    if (responses->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      if (size < sizeof(id)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      if (correlation != id) {
        continue;
      }
      const Bytes payload(buffer.begin() + sizeof(correlation), buffer.begin() + size);
      std::printf("response:\n%s", codec->describe_response(payload).c_str());
      return 0;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      std::fprintf(
        stderr,
        "[ros2] no answer within %.1f s - the service did not run (a bridged service answers "
        "only while the bridge daemon can reach the robot)\n", timeout);
      return 1;
    }
  }
  return 1;
}

// -------------------------------------------------------------- parameters
//
// The parameter services every node serves on its own name
// (`/<node>/get_parameters` and friends, see
// nav2/ros2_shim/include/rclcpp/parameter_service.hpp). They are local
// services like any other, so a call is the call `ros2 service call` makes: a
// correlation id in front of the shim layout of the request on
// `<name>/_request`, the answer on `<name>/_response`. The daemon is not
// involved and cannot be: the gRPC bridge carries topics, services and actions
// the robot advertises, and a *parameter* of a node on this machine is not
// something a remote robot has an opinion about.

// A call to a service served on this machine, with the answer decoded into
// `ServiceT::Response`.
template<class ServiceT>
bool callLocalService(
  const std::string & name, const typename ServiceT::Request & request,
  typename ServiceT::Response & response, double timeout, std::string & error)
{
  auto requests = ros2_bridge::open_or_create_service_channel(name, "request", &error);
  if (!requests) {
    error = "cannot open '" + name + "/_request': " + error;
    return false;
  }
  auto responses = ros2_bridge::open_or_create_service_channel(name, "response", &error);
  if (!responses) {
    error = "cannot open '" + name + "/_response': " + error;
    return false;
  }

  // Start at the head of the answer ring: it keeps what an earlier caller's
  // answer put there, and its correlation ids start at 1 in every new process.
  responses->seek_to_latest();

  const Bytes payload = ros2_shim::serialize(request);
  const std::uint64_t id = 1;
  Bytes out(sizeof(id) + payload.size());
  std::memcpy(out.data(), &id, sizeof(id));
  if (!payload.empty()) {
    std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
  }
  if (!requests->publish(out.data(), out.size())) {
    error = "the request does not fit the '" + name + "/_request' slot";
    return false;
  }

  std::vector<std::uint8_t> buffer(responses->slot_size());
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(static_cast<long long>(std::max(0.0, timeout) * 1000.0));
  while (g_running.load()) {
    std::size_t size = 0;
    if (responses->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      if (size < sizeof(id)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(id));
      if (correlation != id) {
        continue;
      }
      if (!ros2_shim::deserialize(
          buffer.data() + sizeof(correlation), size - sizeof(correlation), response))
      {
        error = "the answer did not decode as ";
        error += ServiceT::Response::ros2_message_type;
        return false;
      }
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "no answer within " + std::to_string(timeout) +
        " s - the node is not polling (is it up and spinning?)";
      return false;
    }
  }
  error = "interrupted";
  return false;
}

// The node named on the command line, as the service prefix it answers
// parameter questions under.
std::string parameterNode(const std::string & argument)
{
  std::string node = qualifyName(argument);
  while (node.size() > 1 && node.back() == '/') {
    node.pop_back();
  }
  return node;
}

// Says which name is missing rather than waiting five seconds for a timeout:
// the shim graph knows every service on this machine.
bool requireService(const std::string & service)
{
  if (findService(collectServices(), service) != nullptr) {
    return true;
  }
  std::fprintf(
    stderr,
    "[ros2] unknown service '%s' (is the node up? `ros2 service list` shows what is reachable; "
    "a parameter service lives on its node's own name)\n",
    service.c_str());
  return false;
}

// One `ros2 param` verb is one service call; this is the part they share.
template<class ServiceT>
bool paramCall(
  const std::string & node, const char * suffix, const typename ServiceT::Request & request,
  typename ServiceT::Response & response, double timeout = 5.0)
{
  const std::string service = node + "/" + suffix;
  if (!requireService(service)) {
    return false;
  }
  std::string error;
  if (!callLocalService<ServiceT>(service, request, response, timeout, error)) {
    std::fprintf(stderr, "[ros2] %s: %s\n", service.c_str(), error.c_str());
    return false;
  }
  return true;
}

// A YAML scalar the params-file reader takes back, which is what makes the
// output of `ros2 param dump` a file `--params-file` can load. A string is
// quoted only where a bare word would read as something else - an empty
// string, a word that looks like a number or a boolean, one with a `:` or a
// `#` in it - so the common case stays readable.
std::string yamlString(const std::string & value)
{
  bool quote = value.empty() || value.front() == ' ' || value.back() == ' ';
  for (char ch : value) {
    if (std::strchr(":#,[]{}&*!|>'\"%@`\\n", ch) != nullptr) {
      quote = true;
      break;
    }
  }
  if (!quote && (value.front() == '-' || value.front() == '?')) {
    quote = true;
  }
  if (!quote) {
    char * end = nullptr;
    std::strtod(value.c_str(), &end);
    quote = (end != value.c_str() && *end == '\0') || value == "true" || value == "false" ||
      value == "null" || value == "~";
  }
  if (!quote) {
    return value;
  }
  std::string quoted = "'";
  for (char ch : value) {
    quoted += ch;
    if (ch == '\'') {
      quoted += '\'';
    }
  }
  return quoted + "'";
}

std::string yamlValue(const rclcpp::ParameterValue & value)
{
  if (value.get_type() == rclcpp::PARAMETER_STRING) {
    return yamlString(value.get<std::string>());
  }
  if (value.get_type() == rclcpp::PARAMETER_STRING_ARRAY) {
    const std::vector<std::string> items = value.get<std::vector<std::string>>();
    std::string text = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
      text += (i == 0 ? "" : ", ") + yamlString(items[i]);
    }
    return text + "]";
  }
  return ros2_shim::format_parameter_value(value);
}

// One node's parameters, one per line, with `-t` naming the type of each.
// Returns false when the node cannot be asked - it never offered parameter
// services, or it is already gone.
bool paramListOne(const std::string & node, bool show_types)
{
  rcl_interfaces::srv::ListParameters::Request request;
  // Recursive: `list` prints the names of every parameter, and the depth
  // answer of the service - the groups a node's parameters are nested under -
  // is what a caller asks for when it wants to walk a tree itself.
  request.depth = rcl_interfaces::srv::ListParameters::Request::DEPTH_RECURSIVE;
  rcl_interfaces::srv::ListParameters::Response response;
  if (!paramCall<rcl_interfaces::srv::ListParameters>(node, "list_parameters", request, response)) {
    return false;
  }

  std::map<std::string, uint8_t> types;
  if (show_types && !response.result.names.empty()) {
    rcl_interfaces::srv::GetParameterTypes::Request types_request;
    types_request.names = response.result.names;
    rcl_interfaces::srv::GetParameterTypes::Response types_response;
    if (paramCall<rcl_interfaces::srv::GetParameterTypes>(
        node, "get_parameter_types", types_request, types_response))
    {
      for (std::size_t i = 0; i < types_response.types.size() && i < types_request.names.size(); ++i) {
        types[types_request.names[i]] = types_response.types[i];
      }
    }
  }

  std::printf("%s:\n", node.c_str());
  for (const std::string & name : response.result.names) {
    if (show_types) {
      const auto it = types.find(name);
      std::printf(
        "  %s [%s]\n", name.c_str(),
        ros2_shim::parameter_type_name(it == types.end() ? 0 : it->second));
    } else {
      std::printf("  %s\n", name.c_str());
    }
  }
  return true;
}

// `ros2 param list [node]`: one node's parameters, or every node's when none is
// named - the same two shapes upstream has.
int cmdParamList(int argc, char ** argv)
{
  std::string node;
  bool show_types = false;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-t" || arg == "--param-type") {
      show_types = true;
    } else if (node.empty()) {
      node = parameterNode(arg);
    } else {
      std::fprintf(stderr, "usage: ros2 param list [node] [-t]\n");
      return 1;
    }
  }

  if (!node.empty()) {
    return paramListOne(node, show_types) ? 0 : 1;
  }

  // No node named: walk the graph. A node that does not expose parameter
  // services at all - `ros2_bridge_daemon` is a plain node - is skipped the way
  // upstream skips it, rather than reported: on a healthy stack that is the
  // normal case, not an error. A node that has the services but will not answer
  // is named once and the walk continues, because one bad node must not hide
  // the nodes after it. An empty graph is not an error either.
  for (const auto & entry : ros2_shim::graph::list_nodes()) {
    if (findService(collectServices(), entry.name + "/list_parameters") == nullptr) {
      continue;
    }
    if (!paramListOne(entry.name, show_types)) {
      std::fprintf(stderr, "[ros2] %s did not answer list_parameters\n", entry.name.c_str());
      continue;
    }
    std::printf("\n");
  }
  return 0;
}

// `ros2 param get <node> <name>`: the value, in the shape upstream prints it.
int cmdParamGet(int argc, char ** argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: ros2 param get <node> <name>\n");
    return 1;
  }
  const std::string node = parameterNode(argv[0]);

  rcl_interfaces::srv::GetParameters::Request request;
  request.names.push_back(argv[1]);
  rcl_interfaces::srv::GetParameters::Response response;
  if (!paramCall<rcl_interfaces::srv::GetParameters>(node, "get_parameters", request, response)) {
    return 1;
  }
  if (response.values.size() != 1 || response.values[0].type == 0) {
    // Upstream's words for both "declared without a value" and "not declared
    // at all", which is what its `get_parameters` will answer too.
    std::printf("Parameter not set\n");
    return 0;
  }
  std::printf(
    "%s value is: %s\n", ros2_shim::parameter_type_name(response.values[0].type),
    ros2_shim::format_parameter_value(response.values[0]).c_str());
  return 0;
}

// `ros2 param set <node> <name> <value>`: one parameter, one value, and a
// refusal that says why.
int cmdParamSet(int argc, char ** argv)
{
  if (argc < 3) {
    std::fprintf(stderr, "usage: ros2 param set <node> <name> <value>\n");
    return 1;
  }
  const std::string node = parameterNode(argv[0]);
  const std::string name = argv[1];

  std::string error;
  const rclcpp::ParameterValue value = ros2_shim::parseParameterText(argv[2], &error);
  if (!error.empty()) {
    std::fprintf(stderr, "[ros2] cannot read '%s': %s\n", argv[2], error.c_str());
    return 1;
  }

  rcl_interfaces::srv::SetParameters::Request request;
  request.parameters.push_back(ros2_shim::to_message(rclcpp::Parameter(name, value)));
  rcl_interfaces::srv::SetParameters::Response response;
  if (!paramCall<rcl_interfaces::srv::SetParameters>(node, "set_parameters", request, response)) {
    return 1;
  }
  if (response.results.empty()) {
    std::fprintf(stderr, "[ros2] %s/set_parameters answered nothing\n", node.c_str());
    return 1;
  }
  if (response.results[0].successful) {
    std::printf("Set parameter successful\n");
    return 0;
  }
  std::printf("Setting parameter failed: %s\n", response.results[0].reason.c_str());
  return 1;
}

int cmdParamDescribe(int argc, char ** argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: ros2 param describe <node> <name>\n");
    return 1;
  }
  const std::string node = parameterNode(argv[0]);

  rcl_interfaces::srv::DescribeParameters::Request request;
  request.names.push_back(argv[1]);
  rcl_interfaces::srv::DescribeParameters::Response response;
  if (!paramCall<rcl_interfaces::srv::DescribeParameters>(
      node, "describe_parameters", request, response))
  {
    return 1;
  }
  if (response.descriptors.empty() || response.descriptors[0].type == 0) {
    std::printf("Parameter not set\n");
    return 0;
  }
  const rcl_interfaces::msg::ParameterDescriptor & descriptor = response.descriptors[0];
  std::printf(
    "%s parameter '%s'\n", ros2_shim::parameter_type_name(descriptor.type), descriptor.name.c_str());
  std::printf("  Description: %s\n", descriptor.description.c_str());
  std::printf(
    "  Constraints: %s\n",
    descriptor.additional_constraints.empty() ? "(none)" :
    descriptor.additional_constraints.c_str());
  std::printf("  Read only: %s\n", descriptor.read_only ? "true" : "false");
  return 0;
}

// `ros2 param dump <node>`: the node's whole parameter set as a params file.
// Two calls - the names, then their values in one go - and the result is a file
// `--params-file` reads back, which is what makes it worth having.
int cmdParamDump(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 param dump <node>\n");
    return 1;
  }
  const std::string node = parameterNode(argv[0]);

  rcl_interfaces::srv::ListParameters::Request list_request;
  list_request.depth = rcl_interfaces::srv::ListParameters::Request::DEPTH_RECURSIVE;
  rcl_interfaces::srv::ListParameters::Response list_response;
  if (!paramCall<rcl_interfaces::srv::ListParameters>(
      node, "list_parameters", list_request, list_response))
  {
    return 1;
  }

  std::printf("%s:\n  ros__parameters:\n", node.c_str());
  if (list_response.result.names.empty()) {
    return 0;
  }

  rcl_interfaces::srv::GetParameters::Request get_request;
  get_request.names = list_response.result.names;
  rcl_interfaces::srv::GetParameters::Response get_response;
  if (!paramCall<rcl_interfaces::srv::GetParameters>(
      node, "get_parameters", get_request, get_response))
  {
    return 1;
  }
  for (std::size_t i = 0; i < get_response.values.size(); ++i) {
    if (get_response.values[i].type == 0) {
      continue;
    }
    std::printf(
      "    %s: %s\n", get_request.names[i].c_str(),
      yamlValue(ros2_shim::from_message(get_response.values[i])).c_str());
  }
  return 0;
}

// ----------------------------------------------------------------- actions

int cmdActionList(int argc, char ** argv)
{
  bool show_types = false;
  bool verbose = false;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-t" || arg == "--show-types") {
      show_types = true;
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
      show_types = true;
    }
  }
  for (const auto & action : collectActions()) {
    if (show_types) {
      std::printf("%s [%s]\n", action.name.c_str(), action.type.empty() ? "?" : action.type.c_str());
    } else {
      std::printf("%s\n", action.name.c_str());
    }
    if (verbose) {
      std::printf(
        "    source=%s\n", action.remote ? "remote (bridged over gRPC)" : "local");
    }
  }
  return 0;
}

int cmdActionType(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 action type <action>\n");
    return 1;
  }
  const auto actions = collectActions();
  const ActionRow * action = findAction(actions, argv[0]);
  if (action == nullptr || action->type.empty()) {
    std::fprintf(stderr, "Unknown action '%s'\n", argv[0]);
    return 1;
  }
  std::printf("%s\n", action->type.c_str());
  return 0;
}

int cmdActionFind(int argc, char ** argv)
{
  if (argc < 1) {
    std::fprintf(stderr, "usage: ros2 action find <regex>\n");
    return 1;
  }
  std::regex pattern(argv[0]);
  for (const auto & action : collectActions()) {
    if (std::regex_search(action.name, pattern)) {
      std::printf("%s\n", action.name.c_str());
    }
  }
  return 0;
}

// The caller half of the shim's action protocol, written out the way
// `rclcpp_action::Client` does it: id in front of the goal, the id echoed on
// every answer. A goal id only has to be unique among the callers of one
// action - the shim's ids are per-process counters, and its channels carry the
// id, not the sender - so one lonely `1` is right for a one-shot command line
// tool.
int cmdActionSendGoal(int argc, char ** argv)
{
  const char * const usage =
    "usage: ros2 action send_goal <action> <type> [values] [--timeout|-t S] [--feedback|-f]\n"
    "       values is one YAML document, e.g. '{order: 10}', or block 'order:' then an\n"
    "       indented '10' - or a list of field.path=value assignments\n";

  std::string name;
  std::string type;
  std::vector<FieldAssign> fields;
  std::string error;
  double timeout = 30.0;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--timeout" || arg == "-t") && i + 1 < argc) {
      timeout = std::atof(argv[++i]);
    } else if (arg == "--feedback" || arg == "-f") {
      // Accepted for command line compatibility: this client already prints
      // every feedback message it receives, so there is nothing to switch on.
    } else if (!arg.empty() && arg.front() == '-' && arg != "-") {
      std::fprintf(stderr, "[ros2] unknown option '%s'\n", arg.c_str());
      std::fputs(usage, stderr);
      return 1;
    } else if (isValueArgument(arg)) {
      if (!readValueArguments(argc, argv, i, fields, error)) {
        std::fprintf(stderr, "[ros2] cannot read the value: %s\n", error.c_str());
        std::fputs(usage, stderr);
        return 1;
      }
    } else if (name.empty()) {
      name = arg;
    } else if (type.empty()) {
      type = arg;
    } else {
      std::fprintf(stderr, "[ros2] unexpected argument '%s'\n", arg.c_str());
      std::fputs(usage, stderr);
      return 1;
    }
  }
  if (name.empty() || type.empty()) {
    std::fputs(usage, stderr);
    return 1;
  }
  name = qualifyName(name);

  const auto actions = collectActions();
  const ActionRow * row = findAction(actions, name);
  if (row == nullptr) {
    std::fprintf(
      stderr,
      "[ros2] unknown action '%s' (is the bridge daemon up? `ros2 action list` shows what is "
      "reachable)\n", name.c_str());
    return 1;
  }
  if (!row->type.empty() && row->type != type) {
    std::fprintf(
      stderr, "[ros2] warning: %s is advertised as %s, sending to it as %s\n", name.c_str(),
      row->type.c_str(), type.c_str());
  }

  const ros2_bridge::ActionCodec * codec = ros2_bridge::find_action_codec(type);
  if (codec == nullptr) {
    std::fprintf(stderr, "[ros2] no codec for action type '%s'; known types:\n", type.c_str());
    for (const auto & known : ros2_bridge::supported_actions()) {
      std::fprintf(stderr, "    %s\n", known.c_str());
    }
    return 1;
  }

  Bytes goal;
  if (!codec->build_goal(fields, goal, error)) {
    std::fprintf(stderr, "[ros2] cannot build the goal: %s\n", error.c_str());
    return 1;
  }
  std::printf("goal:\n%s", codec->describe_goal(goal).c_str());

  auto goals = ros2_bridge::open_or_create_action_channel(name, "goal", &error);
  auto acks = ros2_bridge::open_or_create_action_channel(name, "goal_ack", &error);
  auto feedbacks = ros2_bridge::open_or_create_action_channel(name, "feedback", &error);
  auto results = ros2_bridge::open_or_create_action_channel(name, "result", &error);
  if (!goals || !acks || !feedbacks || !results) {
    std::fprintf(stderr, "[ros2] cannot open the channels of '%s': %s\n", name.c_str(),
      error.c_str());
    return 1;
  }
  // Every answer ring keeps what an earlier run left in it, and goal ids start
  // at one in every process - reading from the tail would hand this call the
  // previous caller's answer. Same rule as `service call`.
  acks->seek_to_latest();
  feedbacks->seek_to_latest();
  results->seek_to_latest();

  const std::uint64_t id = 1;
  Bytes out(sizeof(id) + goal.size());
  std::memcpy(out.data(), &id, sizeof(id));
  if (!goal.empty()) {
    std::memcpy(out.data() + sizeof(id), goal.data(), goal.size());
  }
  if (!goals->publish(out.data(), out.size())) {
    std::fprintf(stderr, "[ros2] the goal does not fit the '%s/_goal' slot\n", name.c_str());
    return 1;
  }

  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(static_cast<long long>(std::max(0.0, timeout) * 1000.0));
  std::vector<std::uint8_t> buffer(results->slot_size());

  // The acceptance first, so a rejected goal is reported as such instead of
  // being waited out.
  bool accepted = false;
  while (g_running.load()) {
    std::size_t size = 0;
    if (acks->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      if (size < sizeof(id)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      if (correlation != id) {
        continue;
      }
      accepted = size > sizeof(id) && buffer[sizeof(id)] != 0;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
  }
  if (!accepted) {
    std::fprintf(
      stderr,
      "[ros2] the goal was not accepted within %.1f s (a bridged action answers only while the "
      "bridge daemon can reach the robot)\n", timeout);
    return 1;
  }
  std::printf("goal accepted (goal id %llu)\n", static_cast<unsigned long long>(id));

  while (g_running.load()) {
    std::size_t size = 0;
    if (feedbacks->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      if (size <= sizeof(id)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      if (correlation != id) {
        continue;
      }
      const Bytes payload(buffer.begin() + sizeof(correlation), buffer.begin() + size);
      std::printf("feedback:\n%s", codec->describe_feedback(payload).c_str());
    }
    if (results->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      if (size <= sizeof(id)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      if (correlation != id) {
        continue;
      }
      const std::uint8_t code = buffer[sizeof(id)];
      const Bytes payload(buffer.begin() + sizeof(id) + 1, buffer.begin() + size);
      std::printf(
        "result: %s\n%s", resultCodeName(code), codec->describe_result(payload).c_str());
      // The exit status answers "did the goal do what was asked": a canceled,
      // aborted or failed goal is a non-zero exit for the script that started
      // it, even though the action protocol itself did its job perfectly.
      return code == static_cast<std::uint8_t>(ros2_bridge::ShimResultCode::Succeeded) ? 0 : 1;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      std::fprintf(
        stderr, "[ros2] no result within %.1f s - the action is still running or the bridge "
        "lost the robot\n", timeout);
      return 1;
    }
  }
  return 1;
}

int cmdActionCancel(int argc, char ** argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: ros2 action cancel <action> <goal id>\n");
    return 1;
  }
  const std::string name = qualifyName(argv[0]);
  const std::uint64_t id = std::strtoull(argv[1], nullptr, 10);

  const auto actions = collectActions();
  if (findAction(actions, name) == nullptr) {
    std::fprintf(stderr, "[ros2] unknown action '%s'\n", name.c_str());
    return 1;
  }

  std::string error;
  auto cancels = ros2_bridge::open_or_create_action_channel(name, "cancel", &error);
  auto cancel_acks = ros2_bridge::open_or_create_action_channel(name, "cancel_ack", &error);
  if (!cancels || !cancel_acks) {
    std::fprintf(stderr, "[ros2] cannot open the channels of '%s': %s\n", name.c_str(),
      error.c_str());
    return 1;
  }
  cancel_acks->seek_to_latest();
  std::vector<std::uint8_t> out(sizeof(id));
  std::memcpy(out.data(), &id, sizeof(id));
  if (!cancels->publish(out.data(), out.size())) {
    std::fprintf(stderr, "[ros2] the cancel does not fit the '%s/_cancel' slot\n", name.c_str());
    return 1;
  }

  std::vector<std::uint8_t> buffer(cancel_acks->slot_size());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (g_running.load()) {
    std::size_t size = 0;
    if (cancel_acks->subscribe(buffer.data(), buffer.size(), &size, 100)) {
      if (size < sizeof(id)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      if (correlation != id) {
        continue;
      }
      const bool accepted = size > sizeof(id) && buffer[sizeof(id)] != 0;
      std::printf("cancel %s\n", accepted ? "accepted" : "rejected");
      return accepted ? 0 : 1;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      std::fprintf(stderr, "[ros2] no answer within 5.0 s - nothing served '%s'\n", name.c_str());
      return 1;
    }
  }
  return 1;
}

// ------------------------------------------------------------------- nodes

int cmdNodeList(int argc, char ** argv)
{
  bool verbose = false;
  for (int i = 0; i < argc; ++i) {
    if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
      verbose = true;
    }
  }
  for (const auto & node : ros2_shim::graph::list_nodes()) {
    std::printf("%s\n", node.name.c_str());
    if (verbose) {
      std::printf("    source=%s\n", node.source.c_str());
    }
  }
  return 0;
}

int cmdInterfaceList(int argc, char ** argv)
{
  (void)argc;
  (void)argv;
  for (const auto & type : ros2_bridge::supported_types()) {
    std::printf("%s\n", type.c_str());
  }
  for (const auto & type : ros2_bridge::supported_services()) {
    std::printf("%s\n", type.c_str());
  }
  for (const auto & type : ros2_bridge::supported_actions()) {
    std::printf("%s\n", type.c_str());
  }
  return 0;
}

int cmdBridgeStatus(int argc, char ** argv)
{
  (void)argc;
  (void)argv;
  const auto topics = collectTopics();
  int remote = 0;
  for (const auto & topic : topics) {
    if (topic.remote) {
      ++remote;
    }
  }
  std::printf("graph dir      : %s\n", ros2_shim::graph::detail::graph_dir().string().c_str());
  std::printf("topics         : %zu (%d remote, %zu local)\n", topics.size(), remote,
              topics.size() - static_cast<std::size_t>(remote));
  const auto services = collectServices();
  int remote_services = 0;
  for (const auto & service : services) {
    if (service.remote) {
      ++remote_services;
    }
  }
  std::printf("services       : %zu (%d remote, %zu local)\n", services.size(), remote_services,
              services.size() - static_cast<std::size_t>(remote_services));
  const auto actions = collectActions();
  int remote_actions = 0;
  for (const auto & action : actions) {
    if (action.remote) {
      ++remote_actions;
    }
  }
  std::printf("actions        : %zu (%d remote, %zu local)\n", actions.size(), remote_actions,
              actions.size() - static_cast<std::size_t>(remote_actions));
  std::printf("nodes          : %zu\n", ros2_shim::graph::list_nodes().size());
  std::printf("known types    : %zu\n", ros2_bridge::supported_types().size());
  for (const auto & topic : topics) {
    std::printf(
      "  %-30s %-36s %s\n", topic.name.c_str(), topic.type.c_str(),
      topic.remote ? "[gRPC bridge]" : "[shm]");
  }
  return 0;
}

void printUsage()
{
  std::printf(
    "usage: ros2 <command> [options]\n"
    "\n"
    "Commands\n"
    "  topic list [-t] [-v]              list topics known to the shim graph\n"
    "  topic info <topic>                type and counts for a topic\n"
    "  topic type <topic>                print a topic's type\n"
    "  topic echo <topic> [--count N]    print messages (needs a publisher)\n"
    "  topic hz <topic> [--window N]     measure the message rate\n"
    "  topic pub <topic> <type> [values]  publish a message\n"
    "  topic find <regex>                filter topic names\n"
    "  service list [-t] [-v]            list services known to the shim graph\n"
    "  service type <service>            print a service's type\n"
    "  service find <regex>              filter service names\n"
    "  service call <service> <type> [values]  call a service, print the response\n"
    "  action list [-t] [-v]             list actions known to the shim graph\n"
    "  action type <action>              print an action's type\n"
    "  action find <regex>               filter action names\n"
    "  action send_goal <action> <type> [values]  send a goal, print feedback and result\n"
    "  action cancel <action> <goal id>  cancel a goal that is still running\n"
    "  param list [node] [-t]            list parameters (every node when none is named)\n"
    "  param get <node> <name>           print one parameter\n"
    "  param set <node> <name> <value>   set one parameter\n"
    "  param describe <node> <name>      print a parameter's descriptor\n"
    "  param dump <node>                 print the parameters as a params file\n"
    "  node list [-v]                    list nodes\n"
    "  interface list                    types this bridge can convert\n"
    "  bridge status                     overview of the shim graph\n"
    "\n"
    "`values` is one YAML document, the way real ROS2 takes it - quote it for the shell\n"
    "\n"
    "  ros2 topic pub /cmd_vel geometry_msgs/msg/Twist \"{linear: {x: 0.5}}\"\n"
    "  ros2 topic pub /cmd_vel geometry_msgs/msg/Twist \"linear:\\n  x: 0.5\"   (block style)\n"
    "  ros2 service call /set_paused std_srvs/srv/SetBool \"{data: true}\"\n"
    "  ros2 topic pub /scan sensor_msgs/msg/LaserScan \"{ranges: [1.0, 2.0]}\"\n"
    "\n"
    "or a list of field.path=value assignments, which is what this CLI started with\n"
    "and what the .bat demos are written in\n"
    "\n"
    "  ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.5 data=hi\n"
    "\n"
    "`topic pub` also takes --once|-1, --rate|-r HZ, --times|-t N, --print|-p and\n"
    "--duration S; `service call` takes --timeout S; `action send_goal` takes\n"
    "--timeout|-t S and --feedback|-f; `topic echo` takes --once|-1 next to --count N.\n");
}

}  // namespace

int main(int argc, char ** argv)
{
  std::signal(SIGINT, handleSignal);
  if (argc < 2) {
    printUsage();
    return 1;
  }

  const std::string group = argv[1];
  if (group == "topic" && argc >= 3) {
    const std::string command = argv[2];
    if (command == "list") {
      return cmdTopicList(argc - 3, argv + 3);
    }
    if (command == "info") {
      return cmdTopicInfo(argc - 3, argv + 3);
    }
    if (command == "type") {
      return cmdTopicType(argc - 3, argv + 3);
    }
    if (command == "echo") {
      return cmdTopicEcho(argc - 3, argv + 3);
    }
    if (command == "hz") {
      return cmdTopicHz(argc - 3, argv + 3);
    }
    if (command == "pub") {
      return cmdTopicPub(argc - 3, argv + 3);
    }
    if (command == "find") {
      return cmdTopicFind(argc - 3, argv + 3);
    }
  }
  if (group == "service" && argc >= 3) {
    const std::string command = argv[2];
    if (command == "list") {
      return cmdServiceList(argc - 3, argv + 3);
    }
    if (command == "type") {
      return cmdServiceType(argc - 3, argv + 3);
    }
    if (command == "find") {
      return cmdServiceFind(argc - 3, argv + 3);
    }
    if (command == "call") {
      return cmdServiceCall(argc - 3, argv + 3);
    }
  }
  if (group == "action" && argc >= 3) {
    const std::string command = argv[2];
    if (command == "list") {
      return cmdActionList(argc - 3, argv + 3);
    }
    if (command == "type") {
      return cmdActionType(argc - 3, argv + 3);
    }
    if (command == "find") {
      return cmdActionFind(argc - 3, argv + 3);
    }
    if (command == "send_goal") {
      return cmdActionSendGoal(argc - 3, argv + 3);
    }
    if (command == "cancel") {
      return cmdActionCancel(argc - 3, argv + 3);
    }
  }
  if (group == "param" && argc >= 3) {
    const std::string command = argv[2];
    if (command == "list") {
      return cmdParamList(argc - 3, argv + 3);
    }
    if (command == "get") {
      return cmdParamGet(argc - 3, argv + 3);
    }
    if (command == "set") {
      return cmdParamSet(argc - 3, argv + 3);
    }
    if (command == "describe") {
      return cmdParamDescribe(argc - 3, argv + 3);
    }
    if (command == "dump") {
      return cmdParamDump(argc - 3, argv + 3);
    }
  }
  if (group == "node" && argc >= 3 && std::strcmp(argv[2], "list") == 0) {
    return cmdNodeList(argc - 3, argv + 3);
  }
  if (group == "interface" && argc >= 3 && std::strcmp(argv[2], "list") == 0) {
    return cmdInterfaceList(argc - 3, argv + 3);
  }
  if (group == "bridge" && argc >= 3 && std::strcmp(argv[2], "status") == 0) {
    return cmdBridgeStatus(argc - 3, argv + 3);
  }
  if (group == "--help" || group == "-h" || group == "help") {
    printUsage();
    return 0;
  }

  std::fprintf(stderr, "unknown command:");
  for (int i = 1; i < argc; ++i) {
    std::fprintf(stderr, " %s", argv[i]);
  }
  std::fprintf(stderr, "\n\n");
  printUsage();
  return 1;
}
