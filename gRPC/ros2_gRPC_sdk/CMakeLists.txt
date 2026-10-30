cmake_minimum_required(VERSION 3.16)

project(ros2_grpc_sdk
  VERSION 0.1.0
  DESCRIPTION "Cross-platform C++ SDK that bridges ROS2 graphs over gRPC"
  LANGUAGES CXX)

option(ROS2_GRPC_SDK_WITH_ROS2 "Build the rclcpp backend (requires a ROS2 environment)" OFF)
option(ROS2_GRPC_SDK_BUILD_EXAMPLES "Build the example programs" ON)
option(ROS2_GRPC_SDK_BUILD_TESTS "Build the localhost end-to-end test" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

# Make the default config folder available next to the binaries so executables
# run from the build directory also pick it up.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

find_package(Threads REQUIRED)
# protobuf is looked up in CONFIG mode first: MSYS2 ships one, and it is the
# build the gRPC in that same prefix was compiled against. Debian and Ubuntu
# ship no ProtobufConfig.cmake with libprotobuf-dev at all - the gRPC config in
# the same directory has to fall back to plain `find_package(Protobuf)` for the
# same reason (see /usr/lib/x86_64-linux-gnu/cmake/grpc/gRPCConfig.cmake) - so
# there the search goes through CMake's own FindProtobuf module instead. Both
# routes define the two targets used below, protobuf::protoc and
# protobuf::libprotobuf. bridge/windows/CMakeLists.txt does exactly this.
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
  find_package(Protobuf MODULE REQUIRED)
endif()
# Same lookup as the bridge (see bridge/windows/CMakeLists.txt): CONFIG mode
# first, and the FindgRPC module next to it as the fallback for a Linux
# distribution that ships the libraries without the CMake package.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
  find_package(gRPC MODULE REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Generated protobuf / gRPC sources
# ---------------------------------------------------------------------------
set(ROS2_GRPC_SDK_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/proto/ros2_bridge.proto)
set(ROS2_GRPC_SDK_PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${ROS2_GRPC_SDK_PROTO_OUT})

set(ROS2_GRPC_SDK_GENERATED
  ${ROS2_GRPC_SDK_PROTO_OUT}/ros2_bridge.pb.cc
  ${ROS2_GRPC_SDK_PROTO_OUT}/ros2_bridge.pb.h
  ${ROS2_GRPC_SDK_PROTO_OUT}/ros2_bridge.grpc.pb.cc
  ${ROS2_GRPC_SDK_PROTO_OUT}/ros2_bridge.grpc.pb.h)

add_custom_command(
  OUTPUT ${ROS2_GRPC_SDK_GENERATED}
  COMMAND protobuf::protoc
  ARGS --cpp_out=${ROS2_GRPC_SDK_PROTO_OUT}
       --grpc_out=${ROS2_GRPC_SDK_PROTO_OUT}
       -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       ${ROS2_GRPC_SDK_PROTO}
  DEPENDS ${ROS2_GRPC_SDK_PROTO} protobuf::protoc gRPC::grpc_cpp_plugin
  COMMENT "Generating ros2_bridge protobuf/gRPC sources"
  VERBATIM)

# ---------------------------------------------------------------------------
# SDK library
# ---------------------------------------------------------------------------
set(ROS2_GRPC_SDK_SOURCES
  src/common/status.cpp
  src/common/log.cpp
  src/common/version.cpp
  src/common/cdr.cpp
  src/common/ros2_types.cpp
  src/common/yaml.cpp
  src/common/config.cpp
  src/client/ros2_bridge_client.cpp
  src/discovery/discovery.cpp
  src/server/ros2_bridge_server.cpp
  src/server/mock_backend.cpp
  ${ROS2_GRPC_SDK_GENERATED})

if(ROS2_GRPC_SDK_WITH_ROS2)
  find_package(rclcpp REQUIRED)
  list(APPEND ROS2_GRPC_SDK_SOURCES src/server/rclcpp_backend.cpp)
endif()

add_library(ros2_grpc_sdk ${ROS2_GRPC_SDK_SOURCES})
add_library(ros2_grpc_sdk::ros2_grpc_sdk ALIAS ros2_grpc_sdk)

target_include_directories(ros2_grpc_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${ROS2_GRPC_SDK_PROTO_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(ros2_grpc_sdk
  PRIVATE ROS2_GRPC_SDK_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(ros2_grpc_sdk
  PUBLIC
    Threads::Threads
    gRPC::grpc++
    protobuf::libprotobuf)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(ros2_grpc_sdk
    PRIVATE ROS2_GRPC_SDK_BUILDING_SHARED
    INTERFACE ROS2_GRPC_SDK_SHARED)
endif()

if(ROS2_GRPC_SDK_WITH_ROS2)
  target_link_libraries(ros2_grpc_sdk PUBLIC rclcpp::rclcpp)
  target_compile_definitions(ros2_grpc_sdk PUBLIC ROS2_GRPC_SDK_WITH_ROS2)
endif()

if(WIN32)
  set_target_properties(ros2_grpc_sdk PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  target_link_libraries(ros2_grpc_sdk PUBLIC ws2_32 iphlpapi)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(ROS2_GRPC_SDK_BUILD_EXAMPLES)
  # `agent` is the backend-selectable server (`--backend mock|rclcpp`);
  # `mock_agent` stays the mock-only convenience the localhost tests use.
  foreach(example mock_agent agent list_topics subscribe publish bridge_demo discover)
    add_executable(ros2_grpc_sdk_${example} examples/${example}_main.cpp)
    target_link_libraries(ros2_grpc_sdk_${example} PRIVATE ros2_grpc_sdk)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ROS2_GRPC_SDK_BUILD_TESTS)
  enable_testing()
  add_executable(ros2_grpc_sdk_localhost_e2e tests/localhost_e2e.cpp)
  target_link_libraries(ros2_grpc_sdk_localhost_e2e PRIVATE ros2_grpc_sdk)
  add_test(NAME localhost_e2e COMMAND ros2_grpc_sdk_localhost_e2e)

  add_executable(ros2_grpc_sdk_services_localhost tests/services_localhost.cpp)
  target_link_libraries(ros2_grpc_sdk_services_localhost PRIVATE ros2_grpc_sdk)
  add_test(NAME services_localhost COMMAND ros2_grpc_sdk_services_localhost)

  add_executable(ros2_grpc_sdk_discovery_localhost tests/discovery_localhost.cpp)
  target_link_libraries(ros2_grpc_sdk_discovery_localhost PRIVATE ros2_grpc_sdk)
  add_test(NAME discovery_localhost COMMAND ros2_grpc_sdk_discovery_localhost)

  add_executable(ros2_grpc_sdk_config_localhost tests/config_localhost.cpp)
  target_link_libraries(ros2_grpc_sdk_config_localhost PRIVATE ros2_grpc_sdk)
  add_test(NAME config_localhost COMMAND ros2_grpc_sdk_config_localhost)

  # The action RPCs need a real action server, and this test starts its own, so
  # it is hermetic. It is built only where the two packages below are actually
  # installed: on a Windows shim machine without `rclcpp_action` the rest of the
  # SDK still builds and this one test simply does not exist.
  if(ROS2_GRPC_SDK_WITH_ROS2)
    find_package(rclcpp_action QUIET)
    find_package(action_tutorials_interfaces QUIET)
    if(rclcpp_action_FOUND AND action_tutorials_interfaces_FOUND)
      add_executable(ros2_grpc_sdk_actions_rclcpp tests/actions_rclcpp.cpp)
      target_include_directories(ros2_grpc_sdk_actions_rclcpp PRIVATE
        ${action_tutorials_interfaces_INCLUDE_DIRS})
      target_link_libraries(ros2_grpc_sdk_actions_rclcpp PRIVATE
        ros2_grpc_sdk rclcpp_action::rclcpp_action
        ${action_tutorials_interfaces_LIBRARIES})
      add_test(NAME actions_rclcpp COMMAND ros2_grpc_sdk_actions_rclcpp)
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
install(TARGETS ros2_grpc_sdk
  EXPORT ros2_grpc_sdkTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ros2_grpc_sdkConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ros2_grpc_sdkConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ros2_grpc_sdk)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ros2_grpc_sdkConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(EXPORT ros2_grpc_sdkTargets
  NAMESPACE ros2_grpc_sdk::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ros2_grpc_sdk)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ros2_grpc_sdkConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ros2_grpc_sdkConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ros2_grpc_sdk)
