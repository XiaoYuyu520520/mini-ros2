// ros2_bridge_daemon - keeps a Windows ROS2 shim graph in sync with a remote
// gRPC agent (the simulated robot).
//
// * discovers / connects to an agent (fast TCP sweep, then a persistent gRPC
//   channel with HTTP/2 keepalive),
// * mirrors every remote topic it understands into a Windows shared-memory
//   channel created with the shim's own naming and slot sizes, so nav2 nodes
//   subscribe to e.g. /scan exactly as if the driver ran locally,
// * advertises the mirrored topics in the shim graph registry, so
//   `ros2 topic list` shows them,
// * does the same for services: every remote service the bridge can translate
//   is announced in the shim graph and answered on `<name>/_request`, so a
//   nav2 node's `rclcpp::Client` - or `ros2 service call` - reaches a service
//   running on the robot as if it were served locally,
// * and again for actions: an action the robot serves is played back here over
//   the shim's six action channels (`<name>/_goal`, `_goal_ack`, `_cancel`,
//   `_cancel_ack`, `_feedback`, `_result`), so a goal a local
//   `rclcpp_action::Client` - or `ros2 action send_goal` - sends at `/sim/drive`
//   is answered by the robot, feedback included,
// * forwards local topics (default: /cmd_vel, /goal_pose, /initialpose) back
//   to the agent, which is how a nav2 plan reaches the robot.
// * publishes a ready file once the first connection attempt has settled, so a
//   shell that just started the daemon can run `ros2 topic list` without
//   racing the LAN scan.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ros2_bridge/action_registry.hpp"
#include "ros2_bridge/service_registry.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/type_registry.hpp"
#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/config.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/discovery/discovery.hpp"
#include "ros2_shim/graph.hpp"

namespace
{

using ros2_bridge::ActionCodec;
using ros2_bridge::ServiceCodec;
using ros2_grpc_sdk::Bytes;
using ros2_grpc_sdk::ServiceInfo;
using ros2_grpc_sdk::StatusCode;
using ros2_grpc_sdk::Status;
using ros2_grpc_sdk::TopicInfo;

std::atomic<bool> g_running{true};

void handleSignal(int)
{
  g_running.store(false);
}

std::string env_or(const char * name, const std::string & fallback)
{
  const char * value = std::getenv(name);
  return (value && *value) ? std::string(value) : fallback;
}

struct Options
{
  std::string agent;                       // explicit host:port, empty = discover
  std::vector<int> ports{ros2_grpc_sdk::kDefaultBridgePort};
  std::vector<std::string> subnets;
  int discover_timeout_ms = 3000;
  bool include_loopback = false;
  std::vector<std::string> mirror_filter;  // empty = every supported topic
  // Services mirrored from the agent. `--no-services` turns the whole service
  // half off, `--service REGEX` narrows it down the way --mirror does topics.
  bool services = true;
  std::vector<std::string> service_filter;
  // Actions mirrored from the agent, the same two knobs again: `--no-actions`
  // turns the whole action half off, `--action REGEX` narrows it down.
  bool actions = true;
  std::vector<std::string> action_filter;
  // The reverse direction: Windows publishes, the robot subscribes. The first
  // three are the control loop - nav2 drives with `/cmd_vel`, and a caller hands
  // nav2 a target or a starting pose with `/goal_pose` / `/initialpose`. The last
  // two are nav2 answering back: the plan it produced and the behaviour-tree log
  // that says why. Both are event driven (a plan, a tree transition) rather than
  // periodic, so they cost nothing while the stack is idle - the costmaps are
  // deliberately *not* here, because they republish a few hundred kilobytes on a
  // timer; ask for those with `--uplink /global_costmap/costmap` when needed.
  std::vector<std::string> uplink{
    "/cmd_vel", "/goal_pose", "/initialpose", "/plan", "/behavior_tree_log"};
  // The default list above is replaced by the first `--uplink` on the command
  // line and appended to by every later one. Comma separated values work too,
  // but a .bat cannot pass them: cmd.exe turns the commas into argument
  // separators before the batch file ever sees the string, so repetition is
  // the spelling that survives `ros2_bridge.bat start --uplink A --uplink B`.
  bool uplink_overridden = false;
  int throttle_ms = 0;
  double duration_s = 0;
  int refresh_ms = 2000;
  int status_ms = 5000;
  // How long to wait before scanning for an agent again when discovery came up
  // empty. 0 makes the daemon give up (and exit) like `--once` does.
  int retry_ms = 2000;
  bool print_topics = false;
  bool quiet = false;
  bool once = false;
  // Background mode: reopen stdout/stderr on log files and drop the console.
  // A console process that keeps the handles it inherited from the shell alive
  // also keeps that shell's redirect open, so `cmd /c "ros2.bat topic list |
  // more"` would hang until the daemon exits. Detaching avoids that.
  bool detach = false;
  std::string log_path = env_or("ROS2_BRIDGE_LOG", "ros2_bridge_daemon.log");
  std::string log_err_path = env_or("ROS2_BRIDGE_ERR", "ros2_bridge_daemon.err.log");
  // A detached daemon appends for its whole life and is meant to run for days,
  // so the log has to be bounded by something other than a restart. Measured
  // on 2026-09-17 the file had reached 65 MB / 657 677 lines, up from the
  // 50 MB an earlier audit recorded, with nothing that ever trimmed it. Past
  // this many bytes the log is renamed to `<path>.1` and a fresh one opened,
  // so the pair stays within twice this in the steady state; the live file can
  // overshoot by whatever one ten second window writes. 0 disables the check.
  std::uintmax_t log_max_bytes =
    std::strtoull(env_or("ROS2_BRIDGE_LOG_MAX", "16777216").c_str(), nullptr, 10);
  // Announced to whoever is waiting for the daemon to become useful, see
  // signalReady(); empty disables it.
  std::string ready_file = env_or("ROS2_BRIDGE_READY_FILE", "");
  std::string config_path;
};

void usage()
{
  std::printf(
    "usage: ros2_bridge_daemon [options]\n"
    "\n"
    "  --agent HOST:PORT     connect directly instead of scanning the LAN\n"
    "  --ports P[,P...]      discovery ports              (default 50051)\n"
    "  --subnets CIDR[,...]  subnets to sweep             (default: all local)\n"
    "  --discover-timeout MS total discovery budget       (default 3000)\n"
    "  --loopback            include 127.0.0.1 in the scan\n"
    "  --mirror REGEX        only mirror matching topics  (repeatable)\n"
    "  --service REGEX       only serve matching remote services (repeatable)\n"
    "  --no-services         do not mirror/serve the agent's services\n"
    "  --action REGEX        only serve matching remote actions  (repeatable)\n"
    "  --no-actions          do not mirror/serve the agent's actions\n"
    "  --uplink NAME[,NAME]  a local topic to forward     (repeatable; first one\n"
    "                        replaces the default /cmd_vel,/goal_pose,/initialpose)\n"
    "  --no-uplink           disable the reverse direction\n"
    "                        (defaults: /cmd_vel,/goal_pose,/initialpose,/plan,\n"
    "                                   /behavior_tree_log)\n"
    "  --throttle-ms MS      drop remote messages faster than this period\n"
    "  --duration S          stop after S seconds (0 = forever)\n"
    "  --refresh-ms MS       topic rescan period          (default 2000)\n"
    "  --retry-ms MS         rescan period when no agent is reachable (0 = give up)\n"
    "  --once                scan/sync once, print the graph, then exit\n"
    "  --print-topics        print every mirrored topic at startup\n"
    "  --quiet               no periodic status lines\n"
    "  --detach              background mode: append to log files, drop the console\n"
    "  --log PATH            stdout log file when detached (default $ROS2_BRIDGE_LOG)\n"
    "  --log-err PATH        stderr log file when detached (default $ROS2_BRIDGE_ERR)\n"
    "  --log-max-bytes N     rotate a detached log past N bytes, keeping one\n"
    "                        generation as PATH.1        (default 16777216)\n"
    "  --ready-file PATH     create PATH once the first connect attempt settled\n"
    "  --config PATH         explicit SDK config file (discovery defaults)\n"
    "  --help\n");
}

bool parseOptions(int argc, char ** argv, Options & options)
{
  auto next = [&](int & index) -> std::string {
      if (index + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", argv[index]);
        return {};
      }
      return argv[++index];
    };
  auto split = [](const std::string & text, char separator) {
      std::vector<std::string> parts;
      std::string current;
      for (char c : text) {
        if (c == separator) {
          if (!current.empty()) {
            parts.push_back(current);
          }
          current.clear();
        } else {
          current.push_back(c);
        }
      }
      if (!current.empty()) {
        parts.push_back(current);
      }
      return parts;
    };

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage();
      return false;
    } else if (arg == "--agent") {
      options.agent = next(i);
    } else if (arg == "--ports") {
      options.ports.clear();
      for (const auto & part : split(next(i), ',')) {
        options.ports.push_back(std::atoi(part.c_str()));
      }
    } else if (arg == "--subnets") {
      options.subnets = split(next(i), ',');
    } else if (arg == "--discover-timeout") {
      options.discover_timeout_ms = std::atoi(next(i).c_str());
    } else if (arg == "--loopback") {
      options.include_loopback = true;
    } else if (arg == "--mirror") {
      options.mirror_filter.push_back(next(i));
    } else if (arg == "--service") {
      options.service_filter.push_back(next(i));
    } else if (arg == "--no-services") {
      options.services = false;
    } else if (arg == "--action") {
      options.action_filter.push_back(next(i));
    } else if (arg == "--no-actions") {
      options.actions = false;
    } else if (arg == "--uplink") {
      if (!options.uplink_overridden) {
        options.uplink.clear();
        options.uplink_overridden = true;
      }
      for (const auto & name : split(next(i), ',')) {
        options.uplink.push_back(name);
      }
    } else if (arg == "--no-uplink") {
      options.uplink.clear();
      options.uplink_overridden = true;
    } else if (arg == "--throttle-ms") {
      options.throttle_ms = std::atoi(next(i).c_str());
    } else if (arg == "--duration") {
      options.duration_s = std::atof(next(i).c_str());
    } else if (arg == "--refresh-ms") {
      options.refresh_ms = std::atoi(next(i).c_str());
    } else if (arg == "--retry-ms") {
      options.retry_ms = std::atoi(next(i).c_str());
    } else if (arg == "--once") {
      options.once = true;
    } else if (arg == "--print-topics") {
      options.print_topics = true;
    } else if (arg == "--detach") {
      options.detach = true;
    } else if (arg == "--log") {
      options.log_path = next(i);
    } else if (arg == "--log-err") {
      options.log_err_path = next(i);
    } else if (arg == "--log-max-bytes") {
      options.log_max_bytes = std::strtoull(next(i).c_str(), nullptr, 10);
    } else if (arg == "--ready-file") {
      options.ready_file = next(i);
    } else if (arg == "--quiet") {
      options.quiet = true;
    } else if (arg == "--config") {
      options.config_path = next(i);
    } else {
      std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

bool matchesAny(const std::string & value, const std::vector<std::string> & patterns)
{
  for (const auto & pattern : patterns) {
    try {
      if (std::regex_search(value, std::regex(pattern))) {
        return true;
      }
    } catch (const std::regex_error &) {
      if (value == pattern) {
        return true;
      }
    }
  }
  return false;
}

#if defined(_WIN32)
constexpr const char * kNullDevice = "NUL";
#else
constexpr const char * kNullDevice = "/dev/null";
#endif

// `freopen` carries `warn_unused_result` on glibc and not in MinGW's headers,
// so the fallback names its result and drops it instead of ignoring it: the
// only thing left to do when even the null device will not open is carry on.
void redirect_to_null(FILE * stream)
{
  FILE * opened = std::freopen(kNullDevice, "a", stream);
  (void)opened;
}

// Keep a detached log bounded. The order of the three steps is what makes this
// work on Windows: the C runtime does not open these files with
// FILE_SHARE_DELETE, so renaming a log that is still open fails with a sharing
// violation. Pointing the stream at the null device first closes the file; the
// stream is never left dangling, because every path out of this function ends
// with it open on something.
bool rotate_log(FILE * stream, const std::string & path)
{
  const std::string previous = path + ".1";
  std::error_code failure;
  redirect_to_null(stream);
  std::error_code ignored;
  std::filesystem::remove(previous, ignored);
  // The error_code overload returns void: the code is how it reports.
  std::filesystem::rename(path, previous, failure);
  if (failure) {
    // The file could not be moved aside (someone else holds it). Reopening the
    // same path in append mode is still the right answer: no log at all is
    // worse than a log that is over its cap.
    (void)std::freopen(path.c_str(), "a", stream);
    std::setvbuf(stream, nullptr, _IONBF, 0);
    return false;
  }
  if (std::freopen(path.c_str(), "a", stream) == nullptr) {
    redirect_to_null(stream);
    return false;
  }
  std::setvbuf(stream, nullptr, _IONBF, 0);
  return true;
}

void enforce_log_limit(FILE * stream, const std::string & path, std::uintmax_t max_bytes)
{
  if (max_bytes == 0 || path.empty()) {
    return;
  }
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size <= max_bytes) {
    return;
  }
  const bool rotated = rotate_log(stream, path);
  std::printf("[bridge] log %s reached %llu bytes (cap %llu)%s\n", path.c_str(),
    static_cast<unsigned long long>(size), static_cast<unsigned long long>(max_bytes),
    rotated ? ", rotated to .1" : ", could not rotate; appending");
  std::fflush(stdout);
}

class Bridge
{
public:
  explicit Bridge(Options options)
  : options_(std::move(options))
  {
  }

  int run()
  {
    ros2_shim::graph::ProcessRegistry::instance().add_node("/ros2_bridge_daemon");

    // A file an earlier daemon left behind would tell a waiting shell that
    // this one is already up, before it has looked at the network at all.
    clearReady();

    // Keep scanning until an agent shows up: the robot may boot after the
    // bridge does, and the whole point of the daemon is a connection that
    // stays up without supervision.
    //
    // Bounding the log has to happen on this path too, not only in the control
    // loop below. A robot that stays off leaves the daemon inside this loop
    // forever, where the only thing written is `[bridge] retrying in N ms` -- so
    // a cap enforced only after a successful connect does not cap the process
    // that needs it most. Measured 2026-09-17: detached against an unreachable
    // agent with `--log-max-bytes 200`, the log reached 783 bytes in 90 s and no
    // `.1` was ever produced.
    auto next_log_check = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (g_running.load()) {
      // While an attempt is in flight the graph is in an unknown state, so
      // there is no ready file; it comes back when the attempt has settled.
      clearReady();
      if (connectToAgent()) {
        break;
      }
      if (options_.once || options_.retry_ms <= 0) {
        return 2;
      }
      // Nobody answered. Saying so is what keeps `ros2 topic list` on a
      // machine with no robot in reach from waiting out its whole budget:
      // there is nothing to sync, so there is nothing to wait for either.
      signalReady("no-agent");
      std::printf("[bridge] retrying in %d ms\n", options_.retry_ms);
      std::fflush(stdout);
      client_.disconnect();
      for (int waited = 0; waited < options_.retry_ms && g_running.load(); waited += 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto tick = std::chrono::steady_clock::now();
        if (options_.detach && tick >= next_log_check) {
          enforce_log_limit(stdout, options_.log_path, options_.log_max_bytes);
          enforce_log_limit(stderr, options_.log_err_path, options_.log_max_bytes);
          next_log_check = tick + std::chrono::seconds(10);
        }
      }
    }
    if (!g_running.load()) {
      return 0;
    }
    // From here on the graph already exists and is only refreshed, so the
    // announcement happens exactly once per daemon lifetime.
    if (syncTopics()) {
      syncServices();
      syncActions();
      signalReady("ready");
    }

    if (options_.once) {
      report();
      return 0;
    }

    startUplinks();
    const auto started = std::chrono::steady_clock::now();
    auto next_refresh = started;
    auto next_status = started + std::chrono::milliseconds(options_.status_ms);
    // The log check is a file_size, so it runs on its own ten second tick
    // rather than on the 50 ms control loop. Only a detached daemon has log
    // files to bound: when stdout is the caller's console, PATH names nothing
    // this process owns and cutting the stream over to a file would be a bug.
    next_log_check = started + std::chrono::seconds(10);

    while (g_running.load()) {
      const auto now = std::chrono::steady_clock::now();
      if (options_.duration_s > 0 &&
        std::chrono::duration<double>(now - started).count() >= options_.duration_s)
      {
        break;
      }
      if (now >= next_refresh) {
        const bool scanned = syncTopics();
        syncServices();
        syncActions();
        startUplinks();
        publishReadiness(scanned);
        next_refresh = now + std::chrono::milliseconds(options_.refresh_ms);
      }
      if (!options_.quiet && now >= next_status) {
        report();
        next_status = now + std::chrono::milliseconds(options_.status_ms);
      }
      if (options_.detach && now >= next_log_check) {
        enforce_log_limit(stdout, options_.log_path, options_.log_max_bytes);
        enforce_log_limit(stderr, options_.log_err_path, options_.log_max_bytes);
        next_log_check = now + std::chrono::seconds(10);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    shutdown();
    if (!options_.quiet) {
      std::printf("\n[bridge] final summary\n");
      report();
    }
    return 0;
  }

private:
  struct Mirror
  {
    std::string topic;
    std::string type;
    std::shared_ptr<shm_transport::ShmChannel> channel;
    std::atomic<std::uint64_t> messages{0};
    std::atomic<std::uint64_t> bytes{0};
    std::atomic<std::uint64_t> failures{0};
    std::atomic<std::int64_t> last_stamp_ns{0};
    // Set by the subscription's close callback, read by syncTopics(). A gRPC
    // stream is not immortal (the agent restarts, the sim is bounced, the TCP
    // connection is reset), and a mirrored topic whose stream has ended never
    // writes its shared-memory channel again - so this flag is how the refresh
    // loop learns that the mirror has to be rebuilt.
    std::atomic<bool> live{true};
    // How many times this topic had to be re-subscribed over the daemon's
    // lifetime, carried across mirror objects: the graph is otherwise silent
    // about a robot the bridge had to reconnect to.
    std::atomic<std::uint64_t> reconnects{0};
    std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();
  };

  // Re-subscribing bookkeeping, one entry per topic the agent still advertises.
  struct Retry
  {
    int failures = 0;                 // consecutive streams that ended early
    std::uint64_t reconnects = 0;     // successful re-subscribes so far
    std::chrono::steady_clock::time_point next_attempt{};
  };

  // The first re-subscribe is immediate - an agent that just restarted is
  // reachable again within a second or two - and later ones back off so a
  // robot that stays away does not cost a stream per refresh.
  static constexpr int kMirrorRetryBaseMs = 250;
  static constexpr int kMirrorRetryMaxMs = 8000;
  // A stream that stayed up this long was healthy, so its next failure starts
  // the backoff from the beginning again.
  static constexpr int kMirrorHealthyMs = 5000;

  static int mirrorBackoffMs(int failures)
  {
    long long delay = kMirrorRetryBaseMs;
    for (int i = 1; i < failures && delay < kMirrorRetryMaxMs; ++i) {
      delay *= 2;
    }
    return static_cast<int>(delay > kMirrorRetryMaxMs ? kMirrorRetryMaxMs : delay);
  }

  // One service the robot serves, answered here by the daemon.
  //
  // The shim carries a service call over two shared-memory channels
  // (`<name>/_request`, `<name>/_response`) that hold a correlation id plus
  // the shim's own layout of the request/response - see
  // ros2_shim/include/rclcpp/service.hpp. A remote service has no process here
  // to write the second channel, so the daemon takes that role: it owns both
  // channels, subscribes to the requests, forwards each one over gRPC and
  // publishes the answer back. From the Windows side the pair is
  // indistinguishable from a service served in this machine's own process.
  struct Service
  {
    std::string name;
    std::string type;
    const ServiceCodec * codec = nullptr;
    std::shared_ptr<shm_transport::ShmChannel> requests;
    std::shared_ptr<shm_transport::ShmChannel> responses;
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> failures{0};
    std::atomic<std::int64_t> last_elapsed_ns{0};
    bool running = false;
    std::thread worker;
  };

  // One action the robot serves, played back here by the daemon.
  //
  // The shim carries an action over six shared-memory channels (see
  // bridge/include/ros2_bridge/shm_topic.hpp for the full table). A remote
  // action has no process here to write the server's half of them, so the
  // daemon takes that role: it reads the goals a local caller writes into
  // `<name>/_goal`, forwards each one over gRPC, and publishes the feedback
  // and the result the robot sends back under the same goal id. Cancels take
  // the same route. From the Windows side the six channels are
  // indistinguishable from those of an action served in this machine's own
  // process.
  struct Action
  {
    std::string name;
    std::string type;
    const ActionCodec * codec = nullptr;
    std::shared_ptr<shm_transport::ShmChannel> goals;
    std::shared_ptr<shm_transport::ShmChannel> goal_acks;
    std::shared_ptr<shm_transport::ShmChannel> cancels;
    std::shared_ptr<shm_transport::ShmChannel> cancel_acks;
    std::shared_ptr<shm_transport::ShmChannel> feedbacks;
    std::shared_ptr<shm_transport::ShmChannel> results;
    std::atomic<std::uint64_t> goals_sent{0};
    std::atomic<std::uint64_t> feedbacks_sent{0};
    std::atomic<std::uint64_t> results_sent{0};
    std::atomic<std::uint64_t> failures{0};
    // A local caller's goal id, mapped onto the id the robot gave the goal
    // back. Only cancel needs the pair, but a goal that nobody follows leaves
    // it behind, so it is also what says "this goal is still out there".
    std::mutex mutex;
    std::map<std::uint64_t, std::string> remote_ids;
    // The follower threads of the goals currently running: the streaming call
    // that carries feedback and the result cannot be interleaved with the goal
    // polling in the worker loop, and a goal takes as long as it takes.
    struct Follower
    {
      std::thread thread;
      std::shared_ptr<std::atomic<bool>> done;
    };
    std::vector<Follower> followers;
    bool running = false;
    std::thread worker;
  };

  struct Uplink
  {
    std::string topic;
    std::string type;
    std::shared_ptr<shm_transport::ShmChannel> channel;
    std::atomic<std::uint64_t> messages{0};
    std::atomic<std::uint64_t> failures{0};
    bool running = false;
    // The channel error is worth printing once, not every 200 ms.
    bool reported = false;
    // The shim graph is only re-read every couple of seconds, and a mismatch is
    // reported once per type pair rather than on every cycle.
    std::chrono::steady_clock::time_point last_type_check;
    std::string warned_type;
    std::thread worker;
  };

  // ------------------------------------------------------------- connecting

  bool connectToAgent()
  {
    ros2_grpc_sdk::Config config;
    const Status config_status = ros2_grpc_sdk::loadConfig(config, options_.config_path);
    if (!config_status.ok()) {
      std::fprintf(
        stderr, "[bridge] warning: %s (using defaults)\n", config_status.message().c_str());
    }

    ros2_grpc_sdk::ClientOptions client_options = ros2_grpc_sdk::toClientOptions(config.client);

    if (!options_.agent.empty()) {
      peer_ = options_.agent;
    } else {
      ros2_grpc_sdk::DiscoveryOptions discovery = ros2_grpc_sdk::toDiscoveryOptions(config.discovery);
      if (!options_.ports.empty()) {
        discovery.ports.assign(options_.ports.begin(), options_.ports.end());
      }
      if (!options_.subnets.empty()) {
        discovery.subnets = options_.subnets;
      }
      discovery.overall_timeout_ms = options_.discover_timeout_ms;
      discovery.include_loopback = options_.include_loopback;
      discovery.include_self = false;
      discovery.verify_with_ping = true;

      ros2_grpc_sdk::Ros2BridgeDiscovery scanner(discovery);
      std::vector<ros2_grpc_sdk::DiscoveredAgent> agents;
      std::printf(
        "[bridge] scanning %zu port(s) over %zu subnet(s)...\n", discovery.ports.size(),
        scanner.interfaces().size());
      const Status status = scanner.discover(
        agents, [](std::size_t done, std::size_t total) {
          if (total > 0 && (done == total || done % 64 == 0)) {
            std::printf("\r[bridge] probing %zu/%zu hosts", done, total);
            std::fflush(stdout);
          }
        });
      std::printf("\r");
      if (!status.ok() || agents.empty()) {
        std::fprintf(stderr, "[bridge] no agent found on the LAN\n");
        return false;
      }
      const auto & best = agents.front();
      std::printf(
        "[bridge] found %zu agent(s); using %s (%lld ms, ros_distro=%s)\n", agents.size(),
        best.target().c_str(), static_cast<long long>(best.latency_ms),
        best.ros_distro.empty() ? "?" : best.ros_distro.c_str());
      peer_ = best.target();
    }

    client_options.target = peer_;
    const Status status = client_.connect(client_options);
    if (!status.ok()) {
      std::fprintf(stderr, "[bridge] connect failed: %s\n", status.message().c_str());
      return false;
    }

    std::string echo;
    if (client_.ping("ros2_bridge_daemon", echo).ok()) {
      std::printf("[bridge] handshake ok (echo=%s, peer=%s)\n", echo.c_str(), peer_.c_str());
    }
    return true;
  }

  // --------------------------------------------------------------- mirroring

  // True when the agent answered: the mirrored topics are in the graph then.
  bool syncTopics()
  {
    std::vector<TopicInfo> topics;
    const Status status = client_.listTopics(topics);
    if (!status.ok()) {
      reportScanFailure("listTopics", status.message());
      return false;
    }
    clearScanFailure("listTopics");

    for (const auto & topic : topics) {
      if (topic.name.empty()) {
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_topics_[topic.name] = topic;
        const auto existing = mirrors_.find(topic.name);
        if (existing != mirrors_.end()) {
          // A mirror whose stream is still running is left alone; a dead one
          // has to be dropped here so the code below subscribes again. Without
          // this the topic stayed in `mirrors_` forever, its shared-memory
          // channel stopped being written, and nav2 waited for an /odom that
          // never came - the daemon had to be restarted to get it back.
          if (existing->second->live.load(std::memory_order_acquire)) {
            continue;
          }
          if (!retireMirrorLocked(existing)) {
            continue;  // still backing off from an earlier failed re-subscribe
          }
        }
      }
      // Topics that travel *from* Windows (nav2's /cmd_vel, ...) must never be
      // mirrored: a shim channel has a single writer, and a mirrored /cmd_vel
      // would feed the daemon's own uplink straight back to the robot.
      if (std::find(options_.uplink.begin(), options_.uplink.end(), topic.name) !=
        options_.uplink.end())
      {
        // Still advertise it, so `ros2 topic list` shows the topic and a local
        // `ros2 topic pub` knows which shared-memory channel to write into.
        ros2_shim::graph::ProcessRegistry::instance().set_remote_topic(
          topic.name, topic.type, "remote:" + peer_);
        continue;
      }
      // A topic the peer merely subscribes to has nothing to mirror.
      if (topic.publisher_count == 0 && topic.subscriber_count > 0) {
        continue;
      }
      if (!options_.mirror_filter.empty() && !matchesAny(topic.name, options_.mirror_filter)) {
        continue;
      }
      if (ros2_bridge::find_codec(topic.type) == nullptr) {
        if (!warned_types_.count(topic.type)) {
          warned_types_.insert(topic.type);
          std::printf(
            "[bridge] skipping '%s': no codec for type '%s'\n", topic.name.c_str(),
            topic.type.c_str());
        }
        continue;
      }
      startMirror(topic);
    }
    return true;
  }

  // Called with `mutex_` held: forgets a mirror whose stream has ended so the
  // caller can subscribe again. False means the retry backoff has not elapsed
  // yet and the topic should be left alone for now.
  bool retireMirrorLocked(std::map<std::string, std::shared_ptr<Mirror>>::iterator it)
  {
    // A copy, not a reference: the erase below would dangle it.
    const std::shared_ptr<Mirror> mirror = it->second;
    const auto now = std::chrono::steady_clock::now();
    Retry & retry = retries_[mirror->topic];
    const auto lived = std::chrono::duration_cast<std::chrono::milliseconds>(now - mirror->started_at)
                         .count();
    // Only an early end counts as a failure: a stream that ran for a while and
    // then died (the usual restart) is not a symptom of a bad re-subscribe.
    if (lived >= kMirrorHealthyMs) {
      retry.failures = 0;
    }
    if (now < retry.next_attempt) {
      return false;
    }
    retry.failures += 1;
    retry.next_attempt = now + std::chrono::milliseconds(mirrorBackoffMs(retry.failures));
    mirrors_.erase(it);
    std::fprintf(
      stderr, "[bridge] re-subscribing %s (stream ended after %lld ms)\n",
      mirror->topic.c_str(), static_cast<long long>(lived));
    return true;
  }

  // -------------------------------------------------------------- ready file

  // `ros2.bat` and the demo scripts read this file as "the bridge is usable
  // now". The daemon creates it once the first connection attempt has settled
  // - with the mirrored topics in the graph on success, or with nothing to
  // mirror when no agent answered - and removes it while an attempt is in
  // flight, so a shell that just started the daemon can poll for it instead of
  // sleeping for a fixed moment and hoping the LAN scan finished in time.
  void clearReady()
  {
    if (!options_.ready_file.empty()) {
      std::remove(options_.ready_file.c_str());
    }
  }

  void signalReady(const char * status)
  {
    if (options_.ready_file.empty()) {
      return;
    }
    std::size_t topics = 0;
    std::size_t services = 0;
    std::size_t actions = 0;
    std::size_t mirrors = 0;
    std::size_t live = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      topics = remote_topics_.size();
      services = services_.size();
      actions = actions_.size();
      mirrors = mirrors_.size();
      for (const auto & entry : mirrors_) {
        if (entry.second->live.load(std::memory_order_acquire)) {
          live += 1;
        }
      }
    }
    // Written through a temporary file and renamed over the real one: this
    // file is rewritten on every refresh, and a waiter that read a half
    // written line would have to guess what `status=` was going to say.
    const std::string temporary = options_.ready_file + ".tmp";
    {
      std::ofstream out(temporary, std::ios::trunc);
      if (!out) {
        std::fprintf(stderr, "[bridge] cannot write %s\n", temporary.c_str());
        return;
      }
      out << "status=" << status << " peer=" << peer_ << " topics=" << topics
          << " services=" << services << " actions=" << actions
          << " mirrors=" << live << "/" << mirrors << "\n";
    }
    std::remove(options_.ready_file.c_str());
    if (std::rename(temporary.c_str(), options_.ready_file.c_str()) != 0) {
      std::fprintf(stderr, "[bridge] cannot rename %s\n", temporary.c_str());
    }
  }

  // The ready file says "the mirrored topics are in shared memory *now*", which
  // is what `wait_bridge_ready.bat` and the nav2 demo scripts wait for. That is
  // not a moment that happens once: a mirror whose gRPC stream ended is dead
  // until the next refresh has rebuilt it, so a daemon that is up but busy
  // re-subscribing must not claim to be ready, and one whose agent went away
  // has to fall back to the same "nothing to mirror" state the cold start uses.
  void publishReadiness(bool scanned)
  {
    if (!scanned) {
      signalReady("no-agent");
      return;
    }
    std::size_t mirrors = 0;
    std::size_t live = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      mirrors = mirrors_.size();
      for (const auto & entry : mirrors_) {
        if (entry.second->live.load(std::memory_order_acquire)) {
          live += 1;
        }
      }
    }
    if (live == mirrors) {
      signalReady("ready");
    } else {
      clearReady();
    }
  }

  // A robot that is switched off fails every scan, and the same gRPC paragraph
  // every two seconds buries everything else the log has to say. The first
  // failure is reported, then one in every kScanFailureLogEvery.
  static constexpr int kScanFailureLogEvery = 30;

  void reportScanFailure(const char * what, const std::string & message)
  {
    int failures = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      failures = ++scan_failures_[what];
    }
    if (failures == 1 || failures % kScanFailureLogEvery == 0) {
      std::fprintf(
        stderr, "[bridge] %s failed (%d in a row): %s\n", what, failures,
        flatten(message).c_str());
    }
  }

  void clearScanFailure(const char * what)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_failures_.erase(what);
  }

  // gRPC error messages carry newlines ("failed to connect ...\n -- 10061"),
  // and one failure has to stay one greppable log line.
  static std::string flatten(std::string text)
  {
    for (char & c : text) {
      if (c == '\n' || c == '\r' || c == '\t') {
        c = ' ';
      }
    }
    return text;
  }

  void startMirror(const TopicInfo & topic)
  {
    auto mirror = std::make_shared<Mirror>();
    mirror->topic = topic.name;
    mirror->type = topic.type;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto retry = retries_.find(topic.name);
      if (retry != retries_.end()) {
        mirror->reconnects.store(retry->second.reconnects, std::memory_order_relaxed);
      }
    }

    std::string error;
    mirror->channel = ros2_bridge::create_channel(topic.name, &error);
    if (!mirror->channel) {
      std::fprintf(
        stderr, "[bridge] cannot create shared memory for '%s': %s\n", topic.name.c_str(),
        error.c_str());
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (mirrors_.count(topic.name) > 0) {
        return;
      }
      mirrors_[topic.name] = mirror;
    }

    ros2_shim::graph::ProcessRegistry::instance().set_remote_topic(
      topic.name, topic.type, "remote:" + peer_);

    const Status status = client_.subscribe(
      topic.name, topic.type,
      [mirror](const ros2_grpc_sdk::Message & message) {
        onRemoteMessage(mirror, message);
      },
      nullptr,
      [mirror](const Status & status) {
        // The stream is gone: flag the mirror so the next refresh rebuilds it.
        // Only the flag and a counter are touched here - this runs on the
        // subscription's own worker thread, which the refresh thread joins
        // while it re-subscribes, so it must not take any lock.
        mirror->live.store(false, std::memory_order_release);
        std::fprintf(
          stderr, "[bridge] subscription for '%s' closed: %s\n", mirror->topic.c_str(),
          status.toString().c_str());
      },
      options_.throttle_ms);

    if (!status.ok()) {
      std::fprintf(
        stderr, "[bridge] subscribe '%s' failed: %s\n", topic.name.c_str(),
        status.message().c_str());
      std::lock_guard<std::mutex> lock(mutex_);
      mirrors_.erase(topic.name);
      // Counted like an early stream end, so the retry backs off instead of
      // hammering an agent that is not answering subscriptions yet.
      Retry & retry = retries_[topic.name];
      retry.failures += 1;
      retry.next_attempt = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(mirrorBackoffMs(retry.failures));
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      Retry & retry = retries_[topic.name];
      if (retry.failures > 0) {
        retry.reconnects += 1;
        mirror->reconnects.store(retry.reconnects, std::memory_order_relaxed);
        retry.next_attempt = std::chrono::steady_clock::time_point{};
      }
    }
    std::printf(
      "[bridge] mirroring %s [%s] -> shm '%s'\n", topic.name.c_str(), topic.type.c_str(),
      ros2_bridge::channel_name(topic.name).c_str());
  }

  static void onRemoteMessage(const std::shared_ptr<Mirror> & mirror, const ros2_grpc_sdk::Message & message)
  {
    Bytes shim;
    if (message.encoding.empty() || message.encoding == "cdr") {
      if (!ros2_bridge::cdr_to_shim(mirror->type, message.data, shim)) {
        mirror->failures.fetch_add(1);
        return;
      }
    } else if (message.encoding == "shim" || message.encoding == "nav2shim") {
      shim = message.data;
    } else {
      mirror->failures.fetch_add(1);
      return;
    }

    if (!mirror->channel->publish(shim.data(), shim.size())) {
      mirror->failures.fetch_add(1);
      return;
    }
    mirror->messages.fetch_add(1);
    mirror->bytes.fetch_add(shim.size());
    mirror->last_stamp_ns.store(message.stamp_ns);
  }

  // -------------------------------------------------------------- services

  // Mirrors the agent's services: each one gets a graph record and a worker
  // that plays the server role on the shim side. A service this machine
  // already serves is left alone - the shim's channels have exactly one
  // writer, and a local server is authoritative for its own name.
  bool syncServices()
  {
    if (!options_.services) {
      return true;
    }
    std::vector<ServiceInfo> services;
    const Status status = client_.listServices(services);
    if (!status.ok()) {
      reportScanFailure("listServices", status.message());
      return false;
    }
    clearScanFailure("listServices");

    for (const auto & service : services) {
      if (service.name.empty()) {
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (services_.count(service.name) > 0) {
          continue;
        }
      }
      if (local_service_exists(service.name)) {
        continue;
      }
      if (!options_.service_filter.empty() &&
        !matchesAny(service.name, options_.service_filter))
      {
        continue;
      }
      const ServiceCodec * codec = ros2_bridge::find_service_codec(service.type);
      if (codec == nullptr) {
        // Only worth a line once per type: a robot has a handful of types and
        // hundreds of services (every nav2 node has a `~/change_state`).
        if (warned_types_.count(service.type) == 0) {
          warned_types_.insert(service.type);
          std::printf(
            "[bridge] skipping '%s': no codec for service type '%s'\n",
            service.name.c_str(), service.type.c_str());
        }
        continue;
      }
      startService(service, codec);
    }
    return true;
  }

  // ---------------------------------------------------------------- actions

  // Mirrors the agent's action servers, the way syncServices() mirrors its
  // services: each one gets a graph record and a worker that plays the server
  // role on the shim side. An action this machine already serves is left
  // alone - the shim's channels have exactly one writer, and a local server is
  // authoritative for its own name.
  bool syncActions()
  {
    if (!options_.actions) {
      return true;
    }
    std::vector<ros2_grpc_sdk::ActionInfo> actions;
    // `only_active`: an action the agent knows about but nobody serves there
    // has nothing to answer a goal with, and advertising it here would make a
    // local caller's wait_for_action_server() say yes to a name that can only
    // fail.
    const Status status = client_.listActions(actions, std::string(), true);
    if (!status.ok()) {
      reportScanFailure("listActions", status.message());
      return false;
    }
    clearScanFailure("listActions");

    for (const auto & action : actions) {
      if (action.name.empty()) {
        continue;
      }
      // Qualified before it is used for anything: the registry keeps action
      // names the way ROS2 tools print them (`/sim/drive`), and the six
      // channels are opened here from the same name a local client will resolve
      // its own through `Node::get_channel`. An agent that advertised
      // `sim/drive` would otherwise get `_goal` channels under `sim/drive/...`
      // while the client reads `/sim/drive/...`, and the goal would be
      // published into a mapping nobody has open.
      const std::string name = ros2_shim::graph::qualified_action_name(action.name);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (actions_.count(name) > 0) {
          continue;
        }
      }
      if (local_action_exists(name)) {
        continue;
      }
      if (!options_.action_filter.empty() && !matchesAny(name, options_.action_filter)) {
        continue;
      }
      const ActionCodec * codec = ros2_bridge::find_action_codec(action.type);
      if (codec == nullptr) {
        if (warned_types_.count(action.type) == 0) {
          warned_types_.insert(action.type);
          std::printf(
            "[bridge] skipping '%s': no codec for action type '%s'\n",
            name.c_str(), action.type.c_str());
        }
        continue;
      }
      ros2_grpc_sdk::ActionInfo qualified = action;
      qualified.name = name;
      startAction(qualified, codec);
    }
    return true;
  }

  // True when a process on this machine serves the action itself - the action
  // spelling of local_service_exists(). Action records are stored fully
  // qualified (`compute_path_to_pose` as `/compute_path_to_pose`), and so is
  // the name the agent advertises, so the comparison is direct.
  static bool local_action_exists(const std::string & name)
  {
    for (const auto & entry : ros2_shim::graph::list_actions()) {
      if (entry.name == name && entry.source == "local") {
        return true;
      }
    }
    return false;
  }

  void startAction(const ros2_grpc_sdk::ActionInfo & info, const ActionCodec * codec)
  {
    auto action = std::make_shared<Action>();
    action->name = info.name;
    action->type = ros2_bridge::canonical_action_type(info.type);
    action->codec = codec;

    std::string error;
    // Created and held, not opened, for the same reason the service channels
    // are: a `Local\` mapping disappears with its last handle, and a caller
    // that sends its first goal an hour from now still has to find these.
    action->goals = ros2_bridge::create_action_channel(info.name, "goal", &error);
    action->goal_acks = ros2_bridge::create_action_channel(info.name, "goal_ack", &error);
    action->cancels = ros2_bridge::create_action_channel(info.name, "cancel", &error);
    action->cancel_acks = ros2_bridge::create_action_channel(info.name, "cancel_ack", &error);
    action->feedbacks = ros2_bridge::create_action_channel(info.name, "feedback", &error);
    action->results = ros2_bridge::create_action_channel(info.name, "result", &error);
    if (!action->goals || !action->goal_acks || !action->cancels || !action->cancel_acks ||
      !action->feedbacks || !action->results)
    {
      std::fprintf(
        stderr, "[bridge] cannot create the channels for action '%s': %s\n",
        info.name.c_str(), error.c_str());
      return;
    }

    // Positioned at the head of both streams the daemon reads, the way
    // `rclcpp_action::Server` positions its own: the ring outlives the process
    // that wrote it, so a daemon that started in front of a leftover goal would
    // drive the robot towards wherever the last run's caller had asked - and
    // answer a goal nobody sent. The four channels the daemon writes need no
    // positioning; it is the only writer of those.
    action->goals->seek_to_latest();
    action->cancels->seek_to_latest();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (actions_.count(info.name) > 0) {
        return;
      }
      actions_[info.name] = action;
    }

    // Advertised as long as the bridge can answer, which is what makes a
    // caller's wait_for_action_server() say yes instead of timing out.
    ros2_shim::graph::ProcessRegistry::instance().add_action(
      info.name, action->type, "remote:" + peer_);

    if (options_.once) {
      return;
    }
    action->running = true;
    action->worker = std::thread([this, action]() { actionLoop(action); });
    std::printf(
      "[bridge] serving action %s [%s] on shm '%s'\n", info.name.c_str(), action->type.c_str(),
      ros2_bridge::channel_name(ros2_bridge::action_channel(info.name, "goal")).c_str());
  }

  // The server half of the shim's action protocol, minus the goal execution
  // itself: goals and cancels arrive on shared memory, go out over gRPC, and
  // the answers come back on the other channels.
  //
  // Goals are answered honestly. A goal that could not be sent is refused on
  // the ack channel instead of being left to time out, because a caller that
  // is told "no" can try something else, while a caller that is left waiting
  // has nothing to go on - the same reasoning as the service loop's, where a
  // failed call has no response to give and is said out loud instead.
  void actionLoop(const std::shared_ptr<Action> & action)
  {
    const ActionCodec & codec = *action->codec;
    std::vector<std::uint8_t> buffer(action->goals->slot_size());
    while (g_running.load() && action->running) {
      reapFollowers(action);

      std::size_t size = 0;
      // Long enough to stay off the CPU, short enough that a cancel does not
      // sit behind a goal that never comes.
      if (action->goals->subscribe(buffer.data(), buffer.size(), &size, 50)) {
        if (size > sizeof(std::uint64_t)) {
          std::uint64_t id = 0;
          std::memcpy(&id, buffer.data(), sizeof(id));
          handleGoal(action, codec, id, Bytes(buffer.begin() + sizeof(id), buffer.begin() + size));
        }
      }

      size = 0;
      if (!action->cancels->subscribe(
          buffer.data(), buffer.size(), &size, 50))
      {
        continue;
      }
      if (size < sizeof(std::uint64_t)) {
        continue;
      }
      std::uint64_t id = 0;
      std::memcpy(&id, buffer.data(), sizeof(id));
      handleCancel(action, id);
    }
  }

  void handleGoal(
    const std::shared_ptr<Action> & action, const ActionCodec & codec, std::uint64_t id,
    const Bytes & goal_shim)
  {
    Bytes goal_cdr;
    if (!codec.goal_to_cdr(goal_shim, goal_cdr)) {
      action->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': undecodable goal from shared memory\n", action->name.c_str());
      publishFlag(action->goal_acks, id, 0);
      return;
    }

    std::string remote_id;
    const Status status =
      client_.sendActionGoal(action->name, action->type, goal_cdr, remote_id, "cdr");
    if (!status.ok()) {
      action->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': sending the goal failed: %s\n", action->name.c_str(),
        status.toString().c_str());
      publishFlag(action->goal_acks, id, 0);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(action->mutex);
      action->remote_ids[id] = remote_id;
    }
    action->goals_sent.fetch_add(1);
    // The acceptance is the robot's: the agent answered the SendActionGoal RPC
    // with a goal id, so the goal runs.
    publishFlag(action->goal_acks, id, 1);

    auto done = std::make_shared<std::atomic<bool>>(false);
    Action::Follower follower;
    follower.done = done;
    follower.thread = std::thread(
      [this, action, id, remote_id, done]() {
        followGoal(action, id, remote_id);
        done->store(true);
      });
    action->followers.push_back(std::move(follower));
  }

  // Streams one goal to its end: every feedback message the robot sends is
  // republished on `<name>/_feedback`, and the terminal message becomes the
  // result on `<name>/_result`.
  void followGoal(
    const std::shared_ptr<Action> & action, std::uint64_t id, const std::string & remote_id)
  {
    const ActionCodec & codec = *action->codec;
    const Status status = client_.followActionGoal(
      action->name, remote_id,
      [&](const ros2_grpc_sdk::ActionFeedback & message) {
        const bool cdr = message.encoding.empty() || message.encoding == "cdr";
        if (message.terminal()) {
          Bytes result_shim;
          if (!cdr || !codec.result_to_shim(message.result, result_shim)) {
            // A robot that ends a goal without a result payload - a cancel
            // before the goal produced anything is the usual reason - still has
            // to be answered here. Dropping the message would leave the caller
            // waiting out its whole timeout for a goal that is already over, so
            // a default-constructed result goes back instead, which is what
            // upstream hands a caller in the same position. Counted, because
            // the payload really was lost.
            action->failures.fetch_add(1);
            std::fprintf(
              stderr, "[bridge] '%s': no usable result for goal %llu (%zu bytes, status %d%s%s); "
              "answering with an empty one\n",
              action->name.c_str(), static_cast<unsigned long long>(id), message.result.size(),
              static_cast<int>(message.status), message.error.empty() ? "" : ", ",
              message.error.c_str());
            result_shim = codec.default_result;
          }
          publishResult(action->results, id, message.status, result_shim);
          action->results_sent.fetch_add(1);
          return false;  // the goal is over; the stream has nothing left to say
        }
        Bytes feedback_shim;
        if (!cdr || !codec.feedback_to_shim(message.feedback, feedback_shim)) {
          action->failures.fetch_add(1);
          return true;  // one lost feedback message must not kill the goal
        }
        publishPayload(action->feedbacks, id, feedback_shim);
        action->feedbacks_sent.fetch_add(1);
        return true;
      });

    if (!status.ok()) {
      // No result was delivered, so the caller is left to time out on its own
      // - there is nothing to hand it, and inventing a result would be worse
      // than an honest silence. Said out loud so the reason is in the log.
      action->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': following the goal failed: %s\n", action->name.c_str(),
        status.toString().c_str());
    }
    {
      std::lock_guard<std::mutex> lock(action->mutex);
      action->remote_ids.erase(id);
    }
  }

  void handleCancel(const std::shared_ptr<Action> & action, std::uint64_t id)
  {
    std::string remote_id;
    {
      std::lock_guard<std::mutex> lock(action->mutex);
      const auto it = action->remote_ids.find(id);
      if (it != action->remote_ids.end()) {
        remote_id = it->second;
      }
    }
    if (remote_id.empty()) {
      // Either the goal never existed or it has already finished. The shim's
      // own server answers REJECT in both cases rather than staying silent,
      // which would leave the caller waiting out its whole timeout.
      publishFlag(action->cancel_acks, id, 0);
      return;
    }
    const Status status = client_.cancelActionGoal(action->name, remote_id);
    if (!status.ok()) {
      action->failures.fetch_add(1);
      std::fprintf(
        stderr, "[bridge] '%s': the cancel was not delivered: %s\n", action->name.c_str(),
        status.toString().c_str());
    }
    publishFlag(action->cancel_acks, id, status.ok() ? 1 : 0);
  }

  // Joins the follower threads that have finished, so a long lived daemon does
  // not accumulate one per goal ever sent.
  static void reapFollowers(const std::shared_ptr<Action> & action)
  {
    for (auto it = action->followers.begin(); it != action->followers.end(); ) {
      if (!it->done->load()) {
        ++it;
        continue;
      }
      if (it->thread.joinable()) {
        it->thread.join();
      }
      it = action->followers.erase(it);
    }
  }

  // The two shapes every message on these channels has in front of it: a goal
  // id, then the payload. The ack channels carry the single byte the shim's
  // protocol answers a goal or a cancel with.
  static void publishFlag(
    const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id, std::uint8_t value)
  {
    std::vector<std::uint8_t> out(sizeof(id) + 1);
    std::memcpy(out.data(), &id, sizeof(id));
    out[sizeof(id)] = value;
    if (!channel->publish(out.data(), out.size())) {
      std::fprintf(stderr, "[bridge] the answer for goal %llu does not fit its slot\n",
        static_cast<unsigned long long>(id));
    }
  }

  static void publishPayload(
    const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id,
    const Bytes & payload)
  {
    std::vector<std::uint8_t> out(sizeof(id) + payload.size());
    std::memcpy(out.data(), &id, sizeof(id));
    if (!payload.empty()) {
      std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
    }
    if (!channel->publish(out.data(), out.size())) {
      std::fprintf(
        stderr, "[bridge] a %zu byte payload for goal %llu does not fit its slot\n",
        payload.size(), static_cast<unsigned long long>(id));
    }
  }

  // A result is a payload with the result's own code in front of it - the
  // shim's `rclcpp_action::Client` reads that byte to decide between
  // SUCCEEDED, ABORTED and CANCELED (see rclcpp_action/types.hpp).
  static void publishResult(
    const std::shared_ptr<shm_transport::ShmChannel> & channel, std::uint64_t id,
    ros2_grpc_sdk::GoalStatus status, const Bytes & payload)
  {
    std::vector<std::uint8_t> out(sizeof(id) + 1 + payload.size());
    std::memcpy(out.data(), &id, sizeof(id));
    out[sizeof(id)] = static_cast<std::uint8_t>(ros2_bridge::shim_result_code(status));
    if (!payload.empty()) {
      std::memcpy(out.data() + sizeof(id) + 1, payload.data(), payload.size());
    }
    if (!channel->publish(out.data(), out.size())) {
      std::fprintf(
        stderr, "[bridge] a result for goal %llu does not fit its slot\n",
        static_cast<unsigned long long>(id));
    }
  }

  // True when a process on this machine serves the name itself. `list_topics`'
  // sibling: the merged view carries the bridge's own records as
  // `remote:<peer>`, so only the local ones answer this.
  static bool local_service_exists(const std::string & name)
  {
    for (const auto & entry : ros2_shim::graph::list_services()) {
      if (entry.name == name && entry.source == "local") {
        return true;
      }
    }
    return false;
  }

  void startService(const ServiceInfo & info, const ServiceCodec * codec)
  {
    auto service = std::make_shared<Service>();
    service->name = info.name;
    service->type = ros2_bridge::canonical_service_type(info.type);
    service->codec = codec;

    std::string error;
    // Created rather than opened, and held for as long as the mirror lasts:
    // both channels are owned by whoever is advertised as the remote end, and
    // a `Local\` mapping disappears with its last handle. Keeping them here is
    // what lets a client that shows up an hour later still find them.
    service->requests =
      ros2_bridge::create_service_channel(info.name, "request", &error);
    if (!service->requests) {
      std::fprintf(
        stderr, "[bridge] cannot create the request channel for '%s': %s\n",
        info.name.c_str(), error.c_str());
      return;
    }
    service->responses =
      ros2_bridge::create_service_channel(info.name, "response", &error);
    if (!service->responses) {
      std::fprintf(
        stderr, "[bridge] cannot create the response channel for '%s': %s\n",
        info.name.c_str(), error.c_str());
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (services_.count(info.name) > 0) {
        return;
      }
      services_[info.name] = service;
    }

    // Advertised as long as the bridge can answer, which is what makes a
    // client's `wait_for_service()` say yes instead of timing out.
    ros2_shim::graph::ProcessRegistry::instance().add_service(
      info.name, service->type, "remote:" + peer_);

    if (options_.once) {
      return;
    }
    service->running = true;
    service->worker = std::thread([this, service]() { serviceLoop(service); });
    std::printf(
      "[bridge] serving %s [%s] on shm '%s'\n", info.name.c_str(), service->type.c_str(),
      ros2_bridge::channel_name(ros2_bridge::service_channel(info.name, "request")).c_str());
  }

  // The server half of the shim's service protocol: a request arrives with a
  // correlation id in front of it, goes out over gRPC, and the answer goes
  // back on the response channel under the same id.
  //
  // A call that fails has no answer to give - the shim's protocol (like ROS2's
  // own services) carries a response, not a status, and inventing one would
  // hand the caller a default-constructed success. The request is counted and
  // said out loud instead, and the caller times out the way it would for a
  // service that vanished mid-call.
  void serviceLoop(const std::shared_ptr<Service> & service)
  {
    const ServiceCodec & codec = *service->codec;
    std::vector<std::uint8_t> buffer(service->requests->slot_size());
    while (g_running.load() && service->running) {
      std::size_t size = 0;
      if (!service->requests->subscribe(buffer.data(), buffer.size(), &size, 100)) {
        continue;
      }
      if (size < sizeof(std::uint64_t)) {
        continue;
      }
      std::uint64_t correlation = 0;
      std::memcpy(&correlation, buffer.data(), sizeof(correlation));
      const Bytes shim(buffer.begin() + sizeof(correlation), buffer.begin() + size);

      Bytes request_cdr;
      if (!codec.request_to_cdr(shim, request_cdr)) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': undecodable request from shared memory\n",
          service->name.c_str());
        continue;
      }

      ros2_grpc_sdk::ServiceCallResult result;
      const Status status = client_.callService(
        service->name, service->type, request_cdr, result, "cdr");
      if (!status.ok()) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': call failed: %s\n", service->name.c_str(),
          status.toString().c_str());
        continue;
      }
      if (!result.success) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': %s\n", service->name.c_str(), result.error.c_str());
        continue;
      }

      Bytes response_shim;
      if (!codec.response_to_shim(result.response, response_shim)) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': undecodable response for %s\n", service->name.c_str(),
          service->type.c_str());
        continue;
      }

      Bytes out(sizeof(correlation) + response_shim.size());
      std::memcpy(out.data(), &correlation, sizeof(correlation));
      std::memcpy(out.data() + sizeof(correlation), response_shim.data(), response_shim.size());
      if (!service->responses->publish(out.data(), out.size())) {
        service->failures.fetch_add(1);
        std::fprintf(
          stderr, "[bridge] '%s': the answer does not fit the response slot (%zu bytes)\n",
          service->name.c_str(), out.size());
        continue;
      }
      service->calls.fetch_add(1);
      service->last_elapsed_ns.store(result.elapsed_ns);
    }
  }

  // ---------------------------------------------------------------- uplink

  void startUplinks()
  {
    if (options_.uplink.empty()) {
      return;
    }
    // Learn the type of every local topic from the shim graph registry; the
    // publisher may live in another process (nav2, ros2 topic pub, ...).
    std::map<std::string, std::string> local_types;
    for (const auto & entry : ros2_shim::graph::list_topics()) {
      if (entry.source == "local" && !entry.type.empty()) {
        local_types[entry.name] = entry.type;
      }
    }

    for (const auto & topic : options_.uplink) {
      // A local publisher is the common case, but `/cmd_vel` is normally
      // created by nav2 *after* the bridge starts, and the agent already knows
      // the type from its own graph - so fall back to what the peer reports.
      std::string type;
      const auto it = local_types.find(topic);
      if (it != local_types.end()) {
        type = it->second;
      } else {
        const auto remote = remote_topics_.find(topic);
        if (remote != remote_topics_.end()) {
          type = remote->second.type;
        } else {
          TopicInfo info;
          if (client_.getTopicInfo(topic, info).ok()) {
            type = info.type;
          }
        }
      }
      if (type.empty()) {
        // Silent until now, and a silent skip is the worst kind: the uplink is
        // then simply never there, `/cmd_vel` published on this machine goes
        // nowhere, and nothing anywhere says so. Say it once per topic.
        if (!warned_types_.count("uplink-no-type:" + topic)) {
          warned_types_.insert("uplink-no-type:" + topic);
          std::fprintf(
            stderr,
            "[bridge] uplink '%s' skipped: neither the local graph nor the peer knows its type\n",
            topic.c_str());
        }
        continue;
      }
      if (ros2_bridge::find_codec(type) == nullptr) {
        if (!warned_types_.count(type)) {
          warned_types_.insert(type);
          std::printf(
            "[bridge] uplink '%s' skipped: no codec for type '%s'\n", topic.c_str(), type.c_str());
        }
        continue;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (uplinks_.count(topic) > 0) {
        continue;
      }
      auto uplink = std::make_shared<Uplink>();
      uplink->topic = topic;
      uplink->type = type;
      uplink->running = true;
      uplinks_[topic] = uplink;
      // The daemon's reader *is* this topic's local subscription, and nav2
      // decides whether to publish from the subscriber count: the controller
      // only writes /cmd_vel when `get_subscription_count() > 0`, and that
      // answer comes from the shared graph. Without this record the count is
      // zero on every machine here and the controller stays silent.
      ros2_shim::graph::ProcessRegistry::instance().add_topic(topic, type, "sub");
      uplink->worker = std::thread([this, uplink]() { uplinkLoop(uplink); });
      std::printf("[bridge] forwarding %s [%s] to %s\n", topic.c_str(), type.c_str(), peer_.c_str());
    }
  }

  // The type the local publisher of `topic` announced in the shim graph, or
  // empty when nobody publishes it on this machine - the bridge's own record of
  // the topic is `remote:`, so only local records answer this.
  static std::string local_publisher_type(const std::string & topic)
  {
    for (const auto & entry : ros2_shim::graph::list_topics()) {
      if (entry.name == topic && entry.source == "local" && entry.publishers > 0 &&
        !entry.type.empty())
      {
        return entry.type;
      }
    }
    return std::string();
  }

  // Says so when the two ends of an uplinked topic disagree about its type.
  //
  // The peer told the bridge what it expects; the shim channel only carries
  // bytes and the conversion is by type name, so a `/cmd_vel` the local node
  // publishes as geometry_msgs/msg/TwistStamped is read as
  // geometry_msgs/msg/Twist out of the same slot - the header lands where the
  // velocities should be, `linear.x` comes out as a denormal near zero, and the
  // robot stands still while every counter here says the message went out. That
  // is worth a line in the log.
  void check_uplink_type(const std::shared_ptr<Uplink> & uplink)
  {
    const auto now = std::chrono::steady_clock::now();
    if (uplink->last_type_check.time_since_epoch().count() != 0 &&
      now - uplink->last_type_check < std::chrono::seconds(2))
    {
      return;
    }
    uplink->last_type_check = now;

    const std::string local_type = local_publisher_type(uplink->topic);
    if (local_type.empty() || local_type == uplink->type || local_type == uplink->warned_type) {
      return;
    }
    uplink->warned_type = local_type;
    std::fprintf(
      stderr,
      "[bridge] uplink '%s': the local publisher uses %s but the peer expects %s; "
      "a ROS2 topic has one type and these two do not decode into each other, so what "
      "arrives there is nonsense (for /cmd_vel: the robot never moves). Make the "
      "publisher agree - nav2 picks between geometry_msgs/msg/Twist and "
      "geometry_msgs/msg/TwistStamped with its `enable_stamped_cmd_vel` parameter.\n",
      uplink->topic.c_str(), local_type.c_str(), uplink->type.c_str());
  }

  void uplinkLoop(const std::shared_ptr<Uplink> & uplink)
  {
    std::vector<std::uint8_t> buffer(64 * 1024);
    while (g_running.load() && uplink->running) {
      check_uplink_type(uplink);
      if (!uplink->channel) {
        std::string error;
        // Created rather than opened: a `Local\` mapping lives only as long as
        // some process holds it, so `ros2 topic pub --once /cmd_vel` creates
        // the channel, publishes and exits - taking the channel with it. As the
        // owner of this topic (it is advertised as remote in the graph) the
        // daemon creates it and keeps the handle, so a single `--once` publish
        // lands whether it happens before or after nav2 shows up.
        uplink->channel = ros2_bridge::create_channel(uplink->topic, &error);
        if (!uplink->channel) {
          if (!uplink->reported) {
            uplink->reported = true;
            std::fprintf(
              stderr, "[bridge] cannot open '%s' for forwarding: %s\n", uplink->topic.c_str(),
              error.c_str());
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          continue;
        }
        buffer.assign(uplink->channel->slot_size(), 0);
      }

      std::size_t size = 0;
      if (!uplink->channel->subscribe(buffer.data(), buffer.size(), &size, 100)) {
        continue;
      }
      Bytes shim(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
      Bytes cdr;
      if (!ros2_bridge::shim_to_cdr(uplink->type, shim, cdr)) {
        uplink->failures.fetch_add(1);
        continue;
      }
      const Status status = client_.publish(uplink->topic, uplink->type, cdr, "cdr");
      if (!status.ok()) {
        uplink->failures.fetch_add(1);
        continue;
      }
      uplink->messages.fetch_add(1);
    }
  }

  // -------------------------------------------------------------- reporting

  void report()
  {
    std::vector<std::shared_ptr<Mirror>> mirrors;
    std::vector<std::shared_ptr<Uplink>> uplinks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & entry : mirrors_) {
        mirrors.push_back(entry.second);
      }
      for (const auto & entry : uplinks_) {
        uplinks.push_back(entry.second);
      }
    }
    std::vector<std::shared_ptr<Service>> services;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & entry : services_) {
        services.push_back(entry.second);
      }
    }
    std::vector<std::shared_ptr<Action>> actions;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & entry : actions_) {
        actions.push_back(entry.second);
      }
    }
    std::uint64_t reconnects = 0;
    for (const auto & mirror : mirrors) {
      reconnects += mirror->reconnects.load();
    }
    std::printf(
      "[bridge] peer=%s mirrored=%zu services=%zu actions=%zu uplink=%zu reconnects=%llu\n",
      peer_.c_str(), mirrors.size(), services.size(), actions.size(), uplinks.size(),
      static_cast<unsigned long long>(reconnects));
    for (const auto & mirror : mirrors) {
      std::printf(
        "    %-28s %-34s msgs=%-8llu bytes=%-10llu failed=%llu%s\n", mirror->topic.c_str(),
        mirror->type.c_str(), static_cast<unsigned long long>(mirror->messages.load()),
        static_cast<unsigned long long>(mirror->bytes.load()),
        static_cast<unsigned long long>(mirror->failures.load()),
        mirror->live.load() ? "" : "  [reconnecting]");
    }
    for (const auto & service : services) {
      std::printf(
        "  s %-26s %-34s calls=%-8llu failed=%llu\n", service->name.c_str(),
        service->type.c_str(), static_cast<unsigned long long>(service->calls.load()),
        static_cast<unsigned long long>(service->failures.load()));
    }
    for (const auto & action : actions) {
      std::printf(
        "  a %-26s %-34s goals=%-6llu feedback=%-8llu results=%-6llu failed=%llu\n",
        action->name.c_str(), action->type.c_str(),
        static_cast<unsigned long long>(action->goals_sent.load()),
        static_cast<unsigned long long>(action->feedbacks_sent.load()),
        static_cast<unsigned long long>(action->results_sent.load()),
        static_cast<unsigned long long>(action->failures.load()));
    }
    for (const auto & uplink : uplinks) {
      std::printf(
        "  ^ %-26s %-34s msgs=%-8llu failed=%llu\n", uplink->topic.c_str(),
        uplink->type.c_str(), static_cast<unsigned long long>(uplink->messages.load()),
        static_cast<unsigned long long>(uplink->failures.load()));
    }
  }

  void shutdown()
  {
    std::vector<std::shared_ptr<Service>> services;
    std::vector<std::shared_ptr<Action>> actions;
    std::vector<std::shared_ptr<Uplink>> uplinks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto & entry : services_) {
        entry.second->running = false;
        services.push_back(entry.second);
      }
      for (const auto & entry : actions_) {
        entry.second->running = false;
        actions.push_back(entry.second);
      }
      for (const auto & entry : uplinks_) {
        entry.second->running = false;
        uplinks.push_back(entry.second);
      }
    }
    // Stopped before the client is torn down: a worker blocked in a
    // CallService has to be out of the way before the channel goes.
    for (const auto & service : services) {
      if (service->worker.joinable()) {
        service->worker.join();
      }
      ros2_shim::graph::ProcessRegistry::instance().remove_service(
        service->name, "remote:" + peer_);
    }
    // An action's followers are streaming gRPC calls, so they have to be out
    // of the way before the client goes - the same rule the service workers
    // are joined under, and for the same reason.
    for (const auto & action : actions) {
      if (action->worker.joinable()) {
        action->worker.join();
      }
      for (auto & follower : action->followers) {
        if (follower.thread.joinable()) {
          follower.thread.join();
        }
      }
      ros2_shim::graph::ProcessRegistry::instance().remove_action(
        action->name, "remote:" + peer_);
    }
    for (const auto & uplink : uplinks) {
      if (uplink->worker.joinable()) {
        uplink->worker.join();
      }
      ros2_shim::graph::ProcessRegistry::instance().remove_topic(uplink->topic, "sub");
    }
    client_.unsubscribeAll();
    client_.disconnect();
    ros2_shim::graph::ProcessRegistry::instance().clear_topics_from_source("remote:" + peer_);
    ros2_shim::graph::ProcessRegistry::instance().clear_services_from_source("remote:" + peer_);
    ros2_shim::graph::ProcessRegistry::instance().clear_actions_from_source("remote:" + peer_);
    // The graph is being emptied, so the file that claims it is usable has to
    // go as well.
    clearReady();
    std::printf("[bridge] stopped\n");
  }

  Options options_;
  ros2_grpc_sdk::Ros2BridgeClient client_;
  std::string peer_;
  std::mutex mutex_;
  std::map<std::string, std::shared_ptr<Mirror>> mirrors_;
  // Re-subscribe bookkeeping per advertised topic, so a topic that keeps dying
  // does not turn into a stream every refresh.
  std::map<std::string, Retry> retries_;
  // Last topic list reported by the agent, so an uplink can be registered
  // before any Windows-side publisher exists.
  std::map<std::string, TopicInfo> remote_topics_;
  std::map<std::string, std::shared_ptr<Uplink>> uplinks_;
  // The remote services this daemon answers for, keyed by service name.
  std::map<std::string, std::shared_ptr<Service>> services_;
  // And the remote actions, keyed by action name.
  std::map<std::string, std::shared_ptr<Action>> actions_;
  std::set<std::string> warned_types_;
  // Consecutive failed scans per call ("listTopics", ...), for log throttling.
  std::map<std::string, int> scan_failures_;
};

}  // namespace

// ------------------------------------------------------------------- detach
//
// The null-device helper and the log rotation `detach_process` shares with the
// control loop are above, next to it, because the loop is what has to call
// them and a declaration at the bottom would be one the loop cannot see.

// `ros2_bridge.bat start` runs the daemon with cmd's `start /b`; the POSIX
// equivalent is `setsid`. Either way the child inherits
// the caller's handles, and a process that keeps them open also keeps any pipe
// the caller redirected into alive - which makes `ros2.bat topic list | more`
// block until the daemon exits. Point stdout/stderr at the log files and let
// go of the console instead.
//
// Only the last line of that is Windows-only. The redirection itself is the
// same freopen/setvbuf pair on both platforms, so this is one function rather
// than a Windows one and a POSIX stub: a stub would leave `--detach` silently
// meaning "keep the caller's stdout" on Linux, which is exactly the bug the
// Windows version exists to prevent. `kNullDevice` is the one spelling that
// differs.
void detach_process(const std::string & out_path, const std::string & err_path)
{
  if (std::freopen(out_path.c_str(), "a", stdout) == nullptr) {
    redirect_to_null(stdout);
  }
  if (std::freopen(err_path.c_str(), "a", stderr) == nullptr) {
    redirect_to_null(stderr);
  }
  // Unbuffered on purpose: a background daemon that logs to a file must not
  // sit on its own status lines (nobody is going to flush them for it).
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
#if defined(_WIN32)
  FreeConsole();
#endif
}

int main(int argc, char ** argv)
{
  Options options;
  if (!parseOptions(argc, argv, options)) {
    return 1;
  }
  if (options.detach) {
    detach_process(options.log_path, options.log_err_path);
    // Whatever an earlier daemon left behind is trimmed before this one adds
    // to it, so a file that grew while nobody was looking cannot survive a
    // restart just because the cap is only re-checked every ten seconds.
    enforce_log_limit(stdout, options.log_path, options.log_max_bytes);
    enforce_log_limit(stderr, options.log_err_path, options.log_max_bytes);
  }
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
  ros2_grpc_sdk::setLogLevel(ros2_grpc_sdk::LogLevel::Warn);

  Bridge bridge(std::move(options));
  return bridge.run();
}
