#include "ros2_grpc_sdk/version.hpp"

#ifndef ROS2_GRPC_SDK_VERSION_STRING
#define ROS2_GRPC_SDK_VERSION_STRING "0.0.0"
#endif

namespace ros2_grpc_sdk {

const char* version() { return ROS2_GRPC_SDK_VERSION_STRING; }

}  // namespace ros2_grpc_sdk
