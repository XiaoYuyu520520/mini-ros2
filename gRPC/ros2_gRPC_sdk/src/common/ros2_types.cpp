#include "ros2_grpc_sdk/common/ros2_types.hpp"

#include "ros2_grpc_sdk/common/cdr.hpp"
#include "ros2_grpc_sdk/common/log.hpp"

namespace ros2_grpc_sdk::ros2 {

Bytes encodeString(const std::string& value) {
  cdr::Writer writer;
  writer.writeString(value);
  return writer.take();
}

std::string decodeString(const Bytes& payload) {
  cdr::Reader reader(payload);
  const std::string value = reader.readString();
  if (!reader.ok()) {
    ROS2_GRPC_SDK_WARN("decodeString: malformed CDR payload (", payload.size(),
                       " bytes)");
    return {};
  }
  return value;
}

Bytes encodeInt64(std::int64_t value) {
  cdr::Writer writer;
  writer.writeI64(value);
  return writer.take();
}

std::int64_t decodeInt64(const Bytes& payload) {
  cdr::Reader reader(payload);
  const std::int64_t value = reader.readI64();
  if (!reader.ok()) {
    ROS2_GRPC_SDK_WARN("decodeInt64: malformed CDR payload (", payload.size(),
                       " bytes)");
    return 0;
  }
  return value;
}

bool equalPayload(const Bytes& lhs, const Bytes& rhs) { return lhs == rhs; }

}  // namespace ros2_grpc_sdk::ros2
