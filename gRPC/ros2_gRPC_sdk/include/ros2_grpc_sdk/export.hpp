#pragma once

// Shared-library export macro. The SDK is built as a static library by
// default; when BUILD_SHARED_LIBS is on the macro switches to the platform
// specific import/export decoration.
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(ROS2_GRPC_SDK_BUILDING_SHARED)
#define ROS2_GRPC_SDK_API __declspec(dllexport)
#elif defined(ROS2_GRPC_SDK_SHARED)
#define ROS2_GRPC_SDK_API __declspec(dllimport)
#else
#define ROS2_GRPC_SDK_API
#endif
#else
#if defined(ROS2_GRPC_SDK_BUILDING_SHARED)
#define ROS2_GRPC_SDK_API __attribute__((visibility("default")))
#else
#define ROS2_GRPC_SDK_API
#endif
#endif
