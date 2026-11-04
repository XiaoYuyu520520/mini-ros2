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
#include "ros2_bridge/daemon_options.hpp"
#include "ros2_bridge/service_registry.hpp"
#include "ros2_bridge/action_proxy.hpp"
#include "ros2_bridge/service_proxy.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_bridge/topic_mirror.hpp"
#include "ros2_bridge/uplink_forwarder.hpp"
#include "ros2_bridge/topic_mirror.hpp"
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

class Bridge
{
public:
  explicit Bridge(ros2_bridge::DaemonOptions options)
  : options_(std::move(options)),
    mirrors_(makeMirrorContext()),
    services_(makeServiceContext()),
    actions_(makeActionContext()),
    uplinks_(makeUplinkContext())
  {
  }

  // What each service needs from the daemon.
  ros2_bridge::MirrorContext makeMirrorContext()
  {
    ros2_bridge::MirrorContext context;
    context.client = &client_;
    context.peer = &peer_;
    context.uplink_topics = &options_.uplink;
    context.warned_types = warned_types_;
    context.report_scan_failure = [this](const char * what, const std::string & message) {
      reportScanFailure(what, message);
    };
    context.clear_scan_failure = [this](const char * what) {
      clearScanFailure(what);
    };
    return context;
  }

  ros2_bridge::ServiceProxyContext makeServiceContext()
  {
    ros2_bridge::ServiceProxyContext context;
    context.client = &client_;
    context.peer = &peer_;
    context.enabled = options_.services;
    context.filters = &options_.service_filter;
    context.once = options_.once;
    context.warned_types = warned_types_;
    context.report_scan_failure = [this](const char * what, const std::string & message) {
      reportScanFailure(what, message);
    };
    context.clear_scan_failure = [this](const char * what) {
      clearScanFailure(what);
    };
    context.running = [this]() {return g_running.load();};
    return context;
  }

  ros2_bridge::ActionProxyContext makeActionContext()
  {
    ros2_bridge::ActionProxyContext context;
    context.client = &client_;
    context.peer = &peer_;
    context.enabled = options_.actions;
    context.filters = &options_.action_filter;
    context.once = options_.once;
    context.warned_types = warned_types_;
    context.report_scan_failure = [this](const char * what, const std::string & message) {
      reportScanFailure(what, message);
    };
    context.clear_scan_failure = [this](const char * what) {
      clearScanFailure(what);
    };
    context.running = [this]() {return g_running.load();};
    return context;
  }

  ros2_bridge::UplinkContext makeUplinkContext()
  {
    ros2_bridge::UplinkContext context;
    context.client = &client_;
    context.peer = &peer_;
    context.topics = &options_.uplink;
    context.warned_types = warned_types_;
    context.remote_type_of = [this](const std::string & topic, std::string & type) {
      return mirrors_.remote_type_of(topic, type);
    };
    context.running = [this]() {return g_running.load();};
    return context;
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
          ros2_bridge::enforceLogLimit(stdout, options_.log_path, options_.log_max_bytes);
          ros2_bridge::enforceLogLimit(stderr, options_.log_err_path, options_.log_max_bytes);
          next_log_check = tick + std::chrono::seconds(10);
        }
      }
    }
    if (!g_running.load()) {
      return 0;
    }
    // From here on the graph already exists and is only refreshed, so the
    // announcement happens exactly once per daemon lifetime.
    if (mirrors_.sync()) {
      services_.sync();
      actions_.sync();
      signalReady("ready");
    }

    if (options_.once) {
      report();
      return 0;
    }

    uplinks_.sync();
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
        const bool scanned = mirrors_.sync();
        services_.sync();
        actions_.sync();
        uplinks_.sync();
        publishReadiness(scanned);
        next_refresh = now + std::chrono::milliseconds(options_.refresh_ms);
      }
      if (!options_.quiet && now >= next_status) {
        report();
        next_status = now + std::chrono::milliseconds(options_.status_ms);
      }
      if (options_.detach && now >= next_log_check) {
        ros2_bridge::enforceLogLimit(stdout, options_.log_path, options_.log_max_bytes);
        ros2_bridge::enforceLogLimit(stderr, options_.log_err_path, options_.log_max_bytes);
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
    services = services_.service_count();
    actions = actions_.action_count();
    topics = mirrors_.remote_topic_count();
    mirrors = mirrors_.mirror_count();
    live = mirrors_.live_mirror_count();
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
    const std::size_t mirrors = mirrors_.mirror_count();
    const std::size_t live = mirrors_.live_mirror_count();
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

  // -------------------------------------------------------------- services

  // Mirrors the agent's services: each one gets a graph record and a worker
  // that plays the server role on the shim side. A service this machine
  // already serves is left alone - the shim's channels have exactly one
  // writer, and a local server is authoritative for its own name.
  // ---------------------------------------------------------------- actions

  // Mirrors the agent's action servers, the way syncServices() mirrors its
  // services: each one gets a graph record and a worker that plays the server
  // role on the shim side. An action this machine already serves is left
  // alone - the shim's channels have exactly one writer, and a local server is
  // authoritative for its own name.
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

  // ---------------------------------------------------------------- uplink

  // -------------------------------------------------------------- reporting

  void report()
  {
    // Each service contributes its own lines; the daemon only adds them up.
    std::vector<std::string> lines;
    mirrors_.append_report(lines);
    services_.append_report(lines);
    actions_.append_report(lines);
    uplinks_.append_report(lines);
    std::printf(
      "[bridge] peer=%s mirrored=%zu services=%zu actions=%zu uplink=%zu\n",
      peer_.c_str(), mirrors_.mirror_count(), services_.service_count(),
      actions_.action_count(), uplinks_.uplink_count());
    for (const auto & line : lines) {
      std::printf("%s\n", line.c_str());
    }
  }


  void shutdown()
  {
    // Each service stops its own workers and drops its own graph records.
    // The order matters: the workers are joined before the client is torn
    // down, because a worker blocked in a service call has to be out of the
    // way before the channel goes.
    services_.stop();
    actions_.stop();
    uplinks_.stop();
    mirrors_.stop();
    ros2_shim::graph::ProcessRegistry::instance().remove_node("/ros2_bridge_daemon");
    client_.unsubscribeAll();
    client_.disconnect();
  }

  // --------------------------------------------------------------- the state


  // The four directions. Each owns its state, its lock and its workers; the
  // daemon owns the connection they all send on.
  ros2_bridge::TopicMirrorService mirrors_;
  ros2_bridge::ServiceProxyService services_;
  ros2_bridge::ActionProxyService actions_;
  ros2_bridge::UplinkForwarderService uplinks_;

  ros2_bridge::DaemonOptions options_;
  // The one connection to the agent. Every service sends on it; the daemon
  // owns it and is the only thing that connects or disconnects.
  ros2_grpc_sdk::Ros2BridgeClient client_;
  // The agent this daemon talks to, as "host:port".
  std::string peer_;
  // The lock that is left: the scan-failure throttle and the ready file's
  // own bookkeeping. Each of the four services has a lock of its own for the
  // state it owns.
  std::mutex mutex_;
  // The types that were already reported as having no codec, so the warning
  // is printed once per type instead of once per refresh.
  std::shared_ptr<std::set<std::string>> warned_types_ =
    std::make_shared<std::set<std::string>>();
  // How many times a scan failed in a row, per scan; the log says one in
  // every kScanFailureLogEvery.
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
    ros2_bridge::redirectToNull(stdout);
  }
  if (std::freopen(err_path.c_str(), "a", stderr) == nullptr) {
    ros2_bridge::redirectToNull(stderr);
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
  ros2_bridge::DaemonOptions options;
  ros2_bridge::applyEnvironmentDefaults(options);
  if (!ros2_bridge::parseDaemonOptions(argc, argv, options)) {
    return 1;
  }
  if (options.detach) {
    detach_process(options.log_path, options.log_err_path);
    // Whatever an earlier daemon left behind is trimmed before this one adds
    // to it, so a file that grew while nobody was looking cannot survive a
    // restart just because the cap is only re-checked every ten seconds.
    ros2_bridge::enforceLogLimit(stdout, options.log_path, options.log_max_bytes);
    ros2_bridge::enforceLogLimit(stderr, options.log_err_path, options.log_max_bytes);
  }
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
  ros2_grpc_sdk::setLogLevel(ros2_grpc_sdk::LogLevel::Warn);

  Bridge bridge(std::move(options));
  return bridge.run();
}
