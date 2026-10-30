#pragma once

#include <cstdint>
#include <string>

#include "ros2_grpc_sdk/common/types.hpp"
#include "ros2_grpc_sdk/export.hpp"

namespace ros2_grpc_sdk::ros2 {

// Convenience (de)serializers for the handful of builtin messages the SDK
// ships demos for. They all emit/consume a CDR payload compatible with the
// corresponding ROS2 message definition, so a payload produced here can be
// fed straight into `rclcpp::GenericPublisher::publish`.

// std_msgs/msg/String  ->  string data
ROS2_GRPC_SDK_API Bytes encodeString(const std::string& value);
ROS2_GRPC_SDK_API std::string decodeString(const Bytes& payload);

// std_msgs/msg/Int64, example_interfaces/msg/Int64  ->  int64 data
ROS2_GRPC_SDK_API Bytes encodeInt64(std::int64_t value);
ROS2_GRPC_SDK_API std::int64_t decodeInt64(const Bytes& payload);

// Returns true when two CDR payloads are byte identical.
ROS2_GRPC_SDK_API bool equalPayload(const Bytes& lhs, const Bytes& rhs);

}  // namespace ros2_grpc_sdk::ros2
