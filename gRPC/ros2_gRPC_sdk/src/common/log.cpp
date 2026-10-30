#include "ros2_grpc_sdk/common/log.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace ros2_grpc_sdk {
namespace {

std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_log_mutex;

}  // namespace

void setLogLevel(LogLevel level) { g_level.store(level, std::memory_order_relaxed); }

LogLevel logLevel() { return g_level.load(std::memory_order_relaxed); }

const char* toString(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Off:
      return "OFF";
  }
  return "?";
}

bool logEnabled(LogLevel level) {
  const LogLevel threshold = logLevel();
  if (threshold == LogLevel::Off) {
    return false;
  }
  return static_cast<int>(level) >= static_cast<int>(threshold);
}

void logMessage(LogLevel level, const char* file, int line,
                const std::string& message) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::fprintf(stderr, "[ros2_gRPC_sdk %s] %s:%d %s\n", toString(level),
               file ? file : "?", line, message.c_str());
  std::fflush(stderr);
}

}  // namespace ros2_grpc_sdk
