#pragma once

#include <sstream>
#include <string>

#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk {

enum class LogLevel { Trace = 0, Debug, Info, Warn, Error, Off };

// Process wide log level. Defaults to Info.
ROS2_GRPC_SDK_API void setLogLevel(LogLevel level);
ROS2_GRPC_SDK_API LogLevel logLevel();
ROS2_GRPC_SDK_API const char* toString(LogLevel level);

// True when a message at `level` would be emitted at the current threshold.
ROS2_GRPC_SDK_API bool logEnabled(LogLevel level);

// Not intended to be called directly; use the ROS2_GRPC_SDK_LOG macro.
ROS2_GRPC_SDK_API void logMessage(LogLevel level, const char* file, int line,
                                  const std::string& message);

namespace detail {

inline void logAppend(std::ostringstream&) {}

template <typename T, typename... Rest>
void logAppend(std::ostringstream& os, T&& value, Rest&&... rest) {
  os << std::forward<T>(value);
  logAppend(os, std::forward<Rest>(rest)...);
}

template <typename... Args>
std::string logConcat(Args&&... args) {
  std::ostringstream os;
  logAppend(os, std::forward<Args>(args)...);
  return os.str();
}

}  // namespace detail
}  // namespace ros2_grpc_sdk

#define ROS2_GRPC_SDK_LOG(level, ...)                                     \
  do {                                                                    \
    if (::ros2_grpc_sdk::logEnabled(level)) {                             \
      ::ros2_grpc_sdk::logMessage(level, __FILE__, __LINE__,              \
                                  ::ros2_grpc_sdk::detail::logConcat(__VA_ARGS__)); \
    }                                                                     \
  } while (false)

#define ROS2_GRPC_SDK_TRACE(...) ROS2_GRPC_SDK_LOG(::ros2_grpc_sdk::LogLevel::Trace, __VA_ARGS__)
#define ROS2_GRPC_SDK_DEBUG(...) ROS2_GRPC_SDK_LOG(::ros2_grpc_sdk::LogLevel::Debug, __VA_ARGS__)
#define ROS2_GRPC_SDK_INFO(...) ROS2_GRPC_SDK_LOG(::ros2_grpc_sdk::LogLevel::Info, __VA_ARGS__)
#define ROS2_GRPC_SDK_WARN(...) ROS2_GRPC_SDK_LOG(::ros2_grpc_sdk::LogLevel::Warn, __VA_ARGS__)
#define ROS2_GRPC_SDK_ERROR(...) ROS2_GRPC_SDK_LOG(::ros2_grpc_sdk::LogLevel::Error, __VA_ARGS__)
