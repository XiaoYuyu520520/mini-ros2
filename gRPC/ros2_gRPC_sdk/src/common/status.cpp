#include "ros2_grpc_sdk/common/status.hpp"

namespace ros2_grpc_sdk {

const char* toString(StatusCode code) {
  switch (code) {
    case StatusCode::Ok:
      return "OK";
    case StatusCode::InvalidArgument:
      return "INVALID_ARGUMENT";
    case StatusCode::NotFound:
      return "NOT_FOUND";
    case StatusCode::AlreadyExists:
      return "ALREADY_EXISTS";
    case StatusCode::Unavailable:
      return "UNAVAILABLE";
    case StatusCode::Timeout:
      return "TIMEOUT";
    case StatusCode::Cancelled:
      return "CANCELLED";
    case StatusCode::Unimplemented:
      return "UNIMPLEMENTED";
    case StatusCode::Internal:
      return "INTERNAL";
    case StatusCode::Unknown:
      return "UNKNOWN";
  }
  return "UNKNOWN";
}

std::string Status::toString() const {
  if (ok()) {
    return "OK";
  }
  std::string out = ros2_grpc_sdk::toString(code_);
  if (!message_.empty()) {
    out += ": ";
    out += message_;
  }
  return out;
}

}  // namespace ros2_grpc_sdk
