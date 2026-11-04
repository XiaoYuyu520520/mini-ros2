#include "ros2_bridge/daemon_options.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <regex>

namespace ros2_bridge
{
namespace
{

std::string env_or(const char * name, const std::string & fallback)
{
  const char * value = std::getenv(name);
  return (value && *value) ? std::string(value) : fallback;
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

}  // namespace

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

void applyEnvironmentDefaults(DaemonOptions & options)
{
  options.log_path = env_or("ROS2_BRIDGE_LOG", "ros2_bridge_daemon.log");
  options.log_err_path = env_or("ROS2_BRIDGE_ERR", "ros2_bridge_daemon.err.log");
  options.log_max_bytes =
    std::strtoull(env_or("ROS2_BRIDGE_LOG_MAX", "16777216").c_str(), nullptr, 10);
  options.ready_file = env_or("ROS2_BRIDGE_READY_FILE", "");
}

void printDaemonUsage()
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

bool parseDaemonOptions(int argc, char ** argv, DaemonOptions & options)
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
      printDaemonUsage();
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

void redirectToNull(FILE * stream)
{
  redirect_to_null(stream);
}

void enforceLogLimit(FILE * stream, const std::string & path, std::uintmax_t max_bytes)
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

}  // namespace ros2_bridge
