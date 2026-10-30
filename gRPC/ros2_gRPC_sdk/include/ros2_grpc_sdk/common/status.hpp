#pragma once

#include <string>
#include <utility>

#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk {

// Transport independent status code shared by the client and the server API.
enum class StatusCode {
  Ok = 0,
  InvalidArgument,
  NotFound,
  AlreadyExists,
  Unavailable,
  Timeout,
  Cancelled,
  Internal,
  // The peer (or this backend) does not serve the requested RPC family at
  // all, as opposed to "it looked and found nothing".
  Unimplemented,
  Unknown,
};

ROS2_GRPC_SDK_API const char* toString(StatusCode code);

// A small value type describing the outcome of an operation. Modelled after
// the gRPC/Abseil status types but intentionally dependency free so users of
// the SDK never have to include generated protobuf headers.
class ROS2_GRPC_SDK_API Status {
 public:
  Status() = default;
  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status success() { return Status(); }
  static Status error(StatusCode code, std::string message) {
    return Status(code, std::move(message));
  }

  bool ok() const { return code_ == StatusCode::Ok; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

  // "OK" or "<CODE>: <message>".
  std::string toString() const;

 private:
  StatusCode code_ = StatusCode::Ok;
  std::string message_;
};

}  // namespace ros2_grpc_sdk
