#ifndef ROS2_BRIDGE_DAEMON_OPTIONS_HPP
#define ROS2_BRIDGE_DAEMON_OPTIONS_HPP

// The daemon's command line, its environment defaults and the log handling
// that goes with them.
//
// This was the first quarter of `bridge_daemon.cpp` and it is a quarter of
// the file that has nothing to do with bridging: it is configuration. It
// lives in its own file now because the daemon itself was split into four
// services (topics, services, actions, uplinks), and every one of them
// needs the options - the filters, the uplink list, the periods - without
// any of them needing the rest of the daemon.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ros2_grpc_sdk/discovery/discovery.hpp"

namespace ros2_bridge
{

// What the daemon was told to do.
struct DaemonOptions
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
  std::string log_path;
  std::string log_err_path;
  // A detached daemon appends for its whole life and is meant to run for days,
  // so the log has to be bounded by something other than a restart. Measured
  // on 2026-09-17 the file had reached 65 MB / 657 677 lines, up from the
  // 50 MB an earlier audit recorded, with nothing that ever trimmed it. Past
  // this many bytes the log is renamed to `<path>.1` and a fresh one opened,
  // so the pair stays within twice this in the steady state; the live file can
  // overshoot by whatever one ten second window writes. 0 disables the check.
  std::uintmax_t log_max_bytes = 16777216;
  // Announced to whoever is waiting for the daemon to become useful, see
  // signalReady(); empty disables it.
  std::string ready_file;
  std::string config_path;
};

// Reads the environment defaults into the options that have one.
void applyEnvironmentDefaults(DaemonOptions & options);

// The command line. Returns false when the daemon should stop: `--help`
// printed the usage, or an unknown option was given.
bool parseDaemonOptions(int argc, char ** argv, DaemonOptions & options);

void printDaemonUsage();

// Does `value` match any of `patterns`? A pattern that is not a valid
// regular expression is compared literally, so a topic name with brackets in
// it still matches itself.
bool matchesAny(const std::string & value, const std::vector<std::string> & patterns);

// Keeps a detached log bounded: past `max_bytes` the file is renamed to
// `<path>.1` and a fresh one opened.
void enforceLogLimit(FILE * stream, const std::string & path, std::uintmax_t max_bytes);

// Points a stream at the null device. Part of detaching: a stream whose log
// file could not be opened must not keep the caller's console redirect alive.
void redirectToNull(FILE * stream);

}  // namespace ros2_bridge

#endif  // ROS2_BRIDGE_DAEMON_OPTIONS_HPP
