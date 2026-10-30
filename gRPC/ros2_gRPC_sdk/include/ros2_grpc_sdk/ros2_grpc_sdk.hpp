#pragma once

// Umbrella header that pulls in the whole public SDK surface.
//
// Client side (any platform, no ROS2 required):
//   #include <ros2_grpc_sdk/ros2_grpc_sdk.hpp>
//
// Server side:
//   #include <ros2_grpc_sdk/server/ros2_bridge_server.hpp>
//   #include <ros2_grpc_sdk/server/mock_backend.hpp>       // no ROS2 needed
//   #include <ros2_grpc_sdk/server/rclcpp_backend.hpp>     // ROS2 required

#include "ros2_grpc_sdk/client/ros2_bridge_client.hpp"
#include "ros2_grpc_sdk/common/cdr.hpp"
#include "ros2_grpc_sdk/common/config.hpp"
#include "ros2_grpc_sdk/common/log.hpp"
#include "ros2_grpc_sdk/common/ros2_types.hpp"
#include "ros2_grpc_sdk/common/status.hpp"
#include "ros2_grpc_sdk/common/types.hpp"
#include "ros2_grpc_sdk/discovery/discovery.hpp"
#include "ros2_grpc_sdk/server/i_ros2_backend.hpp"
#include "ros2_grpc_sdk/server/mock_backend.hpp"
#include "ros2_grpc_sdk/server/ros2_bridge_server.hpp"
#include "ros2_grpc_sdk/version.hpp"
