#ifndef RCLCPP__LOGGING_HPP_
#define RCLCPP__LOGGING_HPP_

#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
#include <string>

#include "rclcpp/logger.hpp"
#include "rcutils/logging.h"

namespace rclcpp
{
namespace detail
{

enum class LogSeverity
{
  Debug,
  Info,
  Warn,
  Error,
  Fatal,
};

inline const char * severity_name(LogSeverity severity)
{
  switch (severity) {
    case LogSeverity::Debug: return "DEBUG";
    case LogSeverity::Info: return "INFO";
    case LogSeverity::Warn: return "WARN";
    case LogSeverity::Error: return "ERROR";
    case LogSeverity::Fatal: return "FATAL";
  }
  return "INFO";
}

// The minimum severity that is printed. NAV2_SHM_LOG_LEVEL=info (what
// bridge_env.bat exports) silences the planner's DEBUG chatter; leaving the
// variable unset keeps the previous behaviour of printing everything.
inline LogSeverity min_severity()
{
  static const LogSeverity level = []() {
      const char * value = std::getenv("NAV2_SHM_LOG_LEVEL");
      if (!value || !*value) {
        return LogSeverity::Debug;
      }
      std::string text(value);
      for (char & c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (text == "info") {return LogSeverity::Info;}
      if (text == "warn" || text == "warning") {return LogSeverity::Warn;}
      if (text == "error") {return LogSeverity::Error;}
      if (text == "fatal") {return LogSeverity::Fatal;}
      return LogSeverity::Debug;
    }();
  return level;
}

inline bool log_enabled(LogSeverity severity)
{
  return static_cast<int>(severity) >= static_cast<int>(min_severity());
}

// ---------------------------------------------------------------------------
// `/rosout`
// ---------------------------------------------------------------------------
//
// Every `RCLCPP_*` macro in the shim funnels into `log_message` / `log_stream`
// below, which until now wrote to stderr and stopped there.  Upstream rclcpp
// sends the same message onto `/rosout` as well, and that is what makes the
// topic visible on every node of a real robot - it is the one name a
// `ros2 topic list` shows on Ubuntu that the shim did not show at all, and the
// one a log aggregator subscribes to instead of scraping a console.
//
// The publisher cannot be built here.  `Publisher` and `Node` are declared
// later in the include order and a node includes *this* header, so a
// `rclcpp::Publisher<rcl_interfaces::msg::Log>` is not even a complete type at
// this point.  What lives here instead is a seam: `Node`'s constructor - the
// only place that owns both a publisher and the logger a node logs through -
// installs a sink, and the two logging functions hand every message to whatever
// sinks are installed.  A process that builds no node installs none and pays
// one empty-map check per message.
//
// The message is delivered *after* stderr, so a message that reached the
// console always reaches the topic as well: the console is the channel a shim
// user already relies on, and it must not become the less reliable of the two.
struct RosoutRecord
{
  LogSeverity severity = LogSeverity::Info;
  // The logger's name, which for a node logger is the node's name - what
  // upstream writes into `Log::name`.
  const char * logger_name = "";
  const char * text = "";
  // The call site the macro expanded at, for `Log::file` / `Log::function` /
  // `Log::line`. Null when a caller went straight to `log_message`.
  const char * file = nullptr;
  const char * function = nullptr;
  std::size_t line = 0;
};

// An empty `std::function` is a *refusal*, not an absence: a node built with
// `enable_rosout(false)` is registered with no sink so that its own log is
// dropped here instead of being handed to whichever node owns the first
// publisher - which is what "this node is not on the topic" has to mean.
using RosoutSink = std::function<void(const RosoutRecord &)>;

struct RosoutSinkEntry
{
  // The `Node`, so that a node which dies unregisters exactly its own entry and
  // a second node cannot silently take the first one's place.
  const void * owner = nullptr;
  // The name the node logs through, which is the node's name.
  std::string logger_name;
  RosoutSink sink;
};

inline std::mutex & rosout_mutex()
{
  static std::mutex mutex;
  return mutex;
}

inline std::vector<RosoutSinkEntry> & rosout_sinks()
{
  static std::vector<RosoutSinkEntry> sinks;
  return sinks;
}

// `/rosout` is on by default, as it is upstream, and `NAV2_SHM_ROSOUT=0` turns
// the publisher and the IPC off for a process that must not open a channel of
// its own.  `NodeOptions::enable_rosout(false)` is the per-node switch, which is
// what upstream nav2's private helper nodes use.
inline bool rosout_enabled()
{
  static const bool enabled = []() {
      const char * value = std::getenv("NAV2_SHM_ROSOUT");
      return !(value && *value && value[0] == '0');
    }();
  return enabled;
}

inline void add_rosout_sink(const void * owner, const std::string & logger_name, RosoutSink sink)
{
  std::lock_guard<std::mutex> lock(rosout_mutex());
  auto & sinks = rosout_sinks();
  for (auto & entry : sinks) {
    if (entry.owner == owner) {
      entry.logger_name = logger_name;
      entry.sink = std::move(sink);
      return;
    }
  }
  sinks.push_back(RosoutSinkEntry{owner, logger_name, std::move(sink)});
}

inline void remove_rosout_sink(const void * owner)
{
  std::lock_guard<std::mutex> lock(rosout_mutex());
  auto & sinks = rosout_sinks();
  for (auto it = sinks.begin(); it != sinks.end(); ++it) {
    if (it->owner == owner) {
      sinks.erase(it);
      return;
    }
  }
}

// Hands one message to the one sink that should carry it.
//
// The sink is chosen by name, because the name is all a logger has: the message
// goes to the node that logs through `logger_name`.  A logger that no node owns
// - `rclcpp::get_logger("some_library")`, which upstream sends through the
// default `/rosout` publisher - goes to the first node instead, so that a
// message from library code is published rather than dropped.  A logger that
// names a node which declined the topic finds that node's empty sink and stops
// there, which is what makes `enable_rosout(false)` the same silence it is
// upstream.
//
// A sink publishes into a shared-memory ring, which is not a place that may
// log: if it ever did, the message would come straight back here.  The
// thread-local flag turns that into a dropped message rather than unbounded
// recursion, and the `empty()` check ahead of the lock keeps a process that
// publishes no `/rosout` free of the whole thing.
inline void publish_rosout(
  LogSeverity severity, const char * logger_name, const char * text,
  const char * file, const char * function, std::size_t line)
{
  static thread_local bool in_rosout = false;
  if (in_rosout) {
    return;
  }
  std::lock_guard<std::mutex> lock(rosout_mutex());
  auto & sinks = rosout_sinks();
  if (sinks.empty()) {
    return;
  }
  const std::string name(logger_name ? logger_name : "");
  const RosoutSink * chosen = nullptr;
  for (const auto & entry : sinks) {
    if (entry.logger_name == name) {
      chosen = &entry.sink;
      break;
    }
  }
  if (!chosen) {
    for (const auto & entry : sinks) {
      if (entry.sink) {
        chosen = &entry.sink;
        break;
      }
    }
  }
  if (!chosen || !*chosen) {
    return;
  }
  in_rosout = true;
  const RosoutRecord record{severity, logger_name, text, file, function, line};
  (*chosen)(record);
  in_rosout = false;
}

inline void log_message(
  LogSeverity severity, const Logger & logger, const char * file, const char * function,
  std::size_t line, const char * fmt, ...)
{
  if (!log_enabled(severity)) {
    return;
  }
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  std::fprintf(
    stderr, "[%s] [%s] %s\n", severity_name(severity), logger.get_name().c_str(), buffer);
  std::fflush(stderr);
  publish_rosout(severity, logger.get_name().c_str(), buffer, file, function, line);
}

inline void log_stream(
  LogSeverity severity, const Logger & logger, const char * file, const char * function,
  std::size_t line, const std::ostringstream & os)
{
  if (!log_enabled(severity)) {
    return;
  }
  const std::string text = os.str();
  std::fprintf(
    stderr, "[%s] [%s] %s\n", severity_name(severity), logger.get_name().c_str(), text.c_str());
  std::fflush(stderr);
  publish_rosout(severity, logger.get_name().c_str(), text.c_str(), file, function, line);
}

template <class... Args>
inline void log_stream_values(
  LogSeverity severity, const Logger & logger, const char * file, const char * function,
  std::size_t line, Args &&... args)
{
  std::ostringstream os;
  (os << ... << args);
  log_stream(severity, logger, file, function, line, os);
}

}  // namespace detail
}  // namespace rclcpp

// Where the call was written, which upstream copies into the `/rosout` message
// (`Log::file` / `Log::function` / `Log::line`).  One macro so the ten
// expansions below stay readable, and so that a compiler without `__func__`
// can be served by a different definition.
#define RCLCPP_DETAIL_CALL_SITE __FILE__, __func__, __LINE__

#define RCLCPP_DEBUG(logger, ...) \
  rclcpp::detail::log_message( \
    rclcpp::detail::LogSeverity::Debug, logger, RCLCPP_DETAIL_CALL_SITE, __VA_ARGS__)
#define RCLCPP_INFO(logger, ...) \
  rclcpp::detail::log_message( \
    rclcpp::detail::LogSeverity::Info, logger, RCLCPP_DETAIL_CALL_SITE, __VA_ARGS__)
#define RCLCPP_WARN(logger, ...) \
  rclcpp::detail::log_message( \
    rclcpp::detail::LogSeverity::Warn, logger, RCLCPP_DETAIL_CALL_SITE, __VA_ARGS__)
#define RCLCPP_ERROR(logger, ...) \
  rclcpp::detail::log_message( \
    rclcpp::detail::LogSeverity::Error, logger, RCLCPP_DETAIL_CALL_SITE, __VA_ARGS__)
#define RCLCPP_FATAL(logger, ...) \
  rclcpp::detail::log_message( \
    rclcpp::detail::LogSeverity::Fatal, logger, RCLCPP_DETAIL_CALL_SITE, __VA_ARGS__)

#define RCLCPP_DEBUG_STREAM(logger, ...) \
  do { std::ostringstream _rclcpp_oss; _rclcpp_oss << __VA_ARGS__; \
    rclcpp::detail::log_stream( \
      rclcpp::detail::LogSeverity::Debug, logger, RCLCPP_DETAIL_CALL_SITE, _rclcpp_oss); } \
  while (0)
#define RCLCPP_INFO_STREAM(logger, ...) \
  do { std::ostringstream _rclcpp_oss; _rclcpp_oss << __VA_ARGS__; \
    rclcpp::detail::log_stream( \
      rclcpp::detail::LogSeverity::Info, logger, RCLCPP_DETAIL_CALL_SITE, _rclcpp_oss); } \
  while (0)
#define RCLCPP_WARN_STREAM(logger, ...) \
  do { std::ostringstream _rclcpp_oss; _rclcpp_oss << __VA_ARGS__; \
    rclcpp::detail::log_stream( \
      rclcpp::detail::LogSeverity::Warn, logger, RCLCPP_DETAIL_CALL_SITE, _rclcpp_oss); } \
  while (0)
#define RCLCPP_ERROR_STREAM(logger, ...) \
  do { std::ostringstream _rclcpp_oss; _rclcpp_oss << __VA_ARGS__; \
    rclcpp::detail::log_stream( \
      rclcpp::detail::LogSeverity::Error, logger, RCLCPP_DETAIL_CALL_SITE, _rclcpp_oss); } \
  while (0)
#define RCLCPP_FATAL_STREAM(logger, ...) \
  do { std::ostringstream _rclcpp_oss; _rclcpp_oss << __VA_ARGS__; \
    rclcpp::detail::log_stream( \
      rclcpp::detail::LogSeverity::Fatal, logger, RCLCPP_DETAIL_CALL_SITE, _rclcpp_oss); } \
  while (0)

#define RCLCPP_DEBUG_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; RCLCPP_DEBUG(logger, __VA_ARGS__);} } \
  while (0)
#define RCLCPP_INFO_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; RCLCPP_INFO(logger, __VA_ARGS__);} } \
  while (0)
#define RCLCPP_WARN_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; RCLCPP_WARN(logger, __VA_ARGS__);} } \
  while (0)
#define RCLCPP_ERROR_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; RCLCPP_ERROR(logger, __VA_ARGS__);} } \
  while (0)

#define RCLCPP_DEBUG_STREAM_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; \
    RCLCPP_DEBUG_STREAM(logger, __VA_ARGS__);} } while (0)
#define RCLCPP_INFO_STREAM_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; \
    RCLCPP_INFO_STREAM(logger, __VA_ARGS__);} } while (0)
#define RCLCPP_WARN_STREAM_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; \
    RCLCPP_WARN_STREAM(logger, __VA_ARGS__);} } while (0)
#define RCLCPP_ERROR_STREAM_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; \
    RCLCPP_ERROR_STREAM(logger, __VA_ARGS__);} } while (0)

#define RCLCPP_DEBUG_SKIPFIRST(logger, ...) RCLCPP_DEBUG(logger, __VA_ARGS__)
#define RCLCPP_INFO_SKIPFIRST(logger, ...) RCLCPP_INFO(logger, __VA_ARGS__)
#define RCLCPP_WARN_SKIPFIRST(logger, ...) RCLCPP_WARN(logger, __VA_ARGS__)
#define RCLCPP_ERROR_SKIPFIRST(logger, ...) RCLCPP_ERROR(logger, __VA_ARGS__)

#define RCLCPP_THROTTLE_IMPL(severity, logger, clock, duration_ms, ...) \
  do { \
    static std::chrono::steady_clock::time_point _rclcpp_last = \
      std::chrono::steady_clock::now() - std::chrono::hours(24); \
    const auto _rclcpp_now = std::chrono::steady_clock::now(); \
    (void)(clock); \
    if (std::chrono::duration_cast<std::chrono::milliseconds>( \
        _rclcpp_now - _rclcpp_last).count() >= static_cast<long long>(duration_ms)) \
    { \
      _rclcpp_last = _rclcpp_now; \
      severity(logger, __VA_ARGS__); \
    } \
  } while (0)

#define RCLCPP_DEBUG_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_IMPL(RCLCPP_DEBUG, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_INFO_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_IMPL(RCLCPP_INFO, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_WARN_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_IMPL(RCLCPP_WARN, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_ERROR_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_IMPL(RCLCPP_ERROR, logger, clock, duration_ms, __VA_ARGS__)

#define RCLCPP_THROTTLE_STREAM_IMPL(severity_stream, logger, clock, duration_ms, ...) \
  do { \
    static std::chrono::steady_clock::time_point _rclcpp_last = \
      std::chrono::steady_clock::now() - std::chrono::hours(24); \
    const auto _rclcpp_now = std::chrono::steady_clock::now(); \
    (void)(clock); \
    if (std::chrono::duration_cast<std::chrono::milliseconds>( \
        _rclcpp_now - _rclcpp_last).count() >= static_cast<long long>(duration_ms)) \
    { \
      _rclcpp_last = _rclcpp_now; \
      severity_stream(logger, __VA_ARGS__); \
    } \
  } while (0)

#define RCLCPP_DEBUG_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_STREAM_IMPL(RCLCPP_DEBUG_STREAM, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_INFO_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_STREAM_IMPL(RCLCPP_INFO_STREAM, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_WARN_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_STREAM_IMPL(RCLCPP_WARN_STREAM, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_ERROR_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_STREAM_IMPL(RCLCPP_ERROR_STREAM, logger, clock, duration_ms, __VA_ARGS__)

#define RCLCPP_FATAL_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; RCLCPP_FATAL(logger, __VA_ARGS__);} } \
  while (0)
#define RCLCPP_FATAL_STREAM_ONCE(logger, ...) \
  do { static bool _once = false; if (!_once) {_once = true; \
    RCLCPP_FATAL_STREAM(logger, __VA_ARGS__);} } while (0)

// `RCLCPP_<SEV>_EXPRESSION(logger, condition, message...)` prints the message
// only when the condition holds: the assert-shaped logging nav2 reaches for
// ("say something if this list is not empty at the one point it must be",
// "say something if the service call we just made failed"). Every call site in
// the ported tree passes a condition first and a format after it.
#define RCLCPP_DEBUG_EXPRESSION(logger, condition, ...) \
  do { if (condition) {RCLCPP_DEBUG(logger, __VA_ARGS__);} } while (0)
#define RCLCPP_INFO_EXPRESSION(logger, condition, ...) \
  do { if (condition) {RCLCPP_INFO(logger, __VA_ARGS__);} } while (0)
#define RCLCPP_WARN_EXPRESSION(logger, condition, ...) \
  do { if (condition) {RCLCPP_WARN(logger, __VA_ARGS__);} } while (0)
#define RCLCPP_ERROR_EXPRESSION(logger, condition, ...) \
  do { if (condition) {RCLCPP_ERROR(logger, __VA_ARGS__);} } while (0)
#define RCLCPP_FATAL_EXPRESSION(logger, condition, ...) \
  do { if (condition) {RCLCPP_FATAL(logger, __VA_ARGS__);} } while (0)

// `_SKIPFIRST*`: upstream drops the first message so a repeating complaint is
// printed once its stream has settled. Here the first message is the one that
// carries the state a reader would want, so the plain macros above are reused
// and every message is printed - the same choice the `_SKIPFIRST` macros
// further up already make.
#define RCLCPP_DEBUG_SKIPFIRST_STREAM(logger, ...) RCLCPP_DEBUG_STREAM(logger, __VA_ARGS__)
#define RCLCPP_INFO_SKIPFIRST_STREAM(logger, ...) RCLCPP_INFO_STREAM(logger, __VA_ARGS__)
#define RCLCPP_WARN_SKIPFIRST_STREAM(logger, ...) RCLCPP_WARN_STREAM(logger, __VA_ARGS__)
#define RCLCPP_ERROR_SKIPFIRST_STREAM(logger, ...) RCLCPP_ERROR_STREAM(logger, __VA_ARGS__)
#define RCLCPP_FATAL_SKIPFIRST(logger, ...) RCLCPP_FATAL(logger, __VA_ARGS__)
#define RCLCPP_FATAL_SKIPFIRST_STREAM(logger, ...) RCLCPP_FATAL_STREAM(logger, __VA_ARGS__)

#define RCLCPP_THROTTLE_SKIPFIRST_IMPL(severity, logger, clock, duration_ms, ...) \
  do { \
    static std::chrono::steady_clock::time_point _rclcpp_last = \
      std::chrono::steady_clock::now(); \
    const auto _rclcpp_now = std::chrono::steady_clock::now(); \
    (void)(clock); \
    if (std::chrono::duration_cast<std::chrono::milliseconds>( \
        _rclcpp_now - _rclcpp_last).count() >= static_cast<long long>(duration_ms)) \
    { \
      _rclcpp_last = _rclcpp_now; \
      severity(logger, __VA_ARGS__); \
    } \
  } while (0)

#define RCLCPP_THROTTLE_SKIPFIRST_STREAM_IMPL(severity_stream, logger, clock, duration_ms, ...) \
  do { \
    static std::chrono::steady_clock::time_point _rclcpp_last = \
      std::chrono::steady_clock::now(); \
    const auto _rclcpp_now = std::chrono::steady_clock::now(); \
    (void)(clock); \
    if (std::chrono::duration_cast<std::chrono::milliseconds>( \
        _rclcpp_now - _rclcpp_last).count() >= static_cast<long long>(duration_ms)) \
    { \
      _rclcpp_last = _rclcpp_now; \
      severity_stream(logger, __VA_ARGS__); \
    } \
  } while (0)

#define RCLCPP_DEBUG_SKIPFIRST_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_IMPL(RCLCPP_DEBUG, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_INFO_SKIPFIRST_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_IMPL(RCLCPP_INFO, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_WARN_SKIPFIRST_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_IMPL(RCLCPP_WARN, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_ERROR_SKIPFIRST_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_IMPL(RCLCPP_ERROR, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_FATAL_SKIPFIRST_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_IMPL(RCLCPP_FATAL, logger, clock, duration_ms, __VA_ARGS__)

#define RCLCPP_DEBUG_SKIPFIRST_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_STREAM_IMPL(RCLCPP_DEBUG_STREAM, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_INFO_SKIPFIRST_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_STREAM_IMPL(RCLCPP_INFO_STREAM, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_WARN_SKIPFIRST_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_STREAM_IMPL(RCLCPP_WARN_STREAM, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_ERROR_SKIPFIRST_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_STREAM_IMPL(RCLCPP_ERROR_STREAM, logger, clock, duration_ms, __VA_ARGS__)
#define RCLCPP_FATAL_SKIPFIRST_STREAM_THROTTLE(logger, clock, duration_ms, ...) \
  RCLCPP_THROTTLE_SKIPFIRST_STREAM_IMPL(RCLCPP_FATAL_STREAM, logger, clock, duration_ms, __VA_ARGS__)

#endif  // RCLCPP__LOGGING_HPP_
