cmake_minimum_required(VERSION 3.16)
project(ros2_bridge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Workspace layout
#
#   <CPU>/gRPC/ros2_gRPC_sdk   the gRPC transport SDK (rebuilt here as a
#                              static library so the bridge is self contained)
#   <CPU>/nav2/ros2_shim       the shared-memory ROS2 compatibility layer
#   <CPU>/nav2/shm_transport   the shared-memory channel implementation
# ---------------------------------------------------------------------------
get_filename_component(_bridge_default_cpu_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CPU_ROOT "${_bridge_default_cpu_root}" CACHE PATH "Root of the CPU workspace")
set(ROS2_BRIDGE_SDK_ROOT "${CPU_ROOT}/gRPC/ros2_gRPC_sdk" CACHE PATH "gRPC SDK root")
set(ROS2_BRIDGE_SHIM_ROOT "${CPU_ROOT}/nav2/ros2_shim" CACHE PATH "ros2 shim root")
set(ROS2_BRIDGE_SHM_ROOT "${CPU_ROOT}/nav2/shm_transport" CACHE PATH "shm_transport root")

# gRPC/protobuf come from the same MSYS2 UCRT64 prefix the SDK uses; appending
# it keeps a plain `cmake -S ... -B ...` invocation working. Pass
# -DCMAKE_PREFIX_PATH=... to use a different toolchain. Off Windows the prefix
# does not exist, so it is not appended and the system packages are used - that
# is the intended Linux path, not a fallback.
set(ROS2_BRIDGE_MSYS2_ROOT "D:/WindowsServer2025_1/software/MSYS2/ucrt64"
    CACHE PATH "MSYS2 UCRT64 prefix providing gRPC and protobuf")
if(EXISTS "${ROS2_BRIDGE_MSYS2_ROOT}/lib/cmake/grpc")
  list(APPEND CMAKE_PREFIX_PATH "${ROS2_BRIDGE_MSYS2_ROOT}")
endif()

find_package(Threads REQUIRED)
# protobuf is looked up in CONFIG mode first: that is what MSYS2 ships, and it
# is the build the gRPC in that same prefix was compiled against. Debian and
# Ubuntu ship *no* ProtobufConfig.cmake at all with libprotobuf-dev - the gRPC
# config in the same directory has to fall back to plain
# `find_package(Protobuf)` for the same reason (see
# /usr/lib/x86_64-linux-gnu/cmake/grpc/gRPCConfig.cmake) - so there the search
# has to go through CMake's own FindProtobuf module instead. Both routes find
# the same 3.21.12 headers and libraries and define both targets used below,
# protobuf::protoc and protobuf::libprotobuf.
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
  find_package(Protobuf MODULE REQUIRED)
endif()
# gRPC is looked up in CONFIG mode first (MSYS2 and the Debian/Ubuntu dev
# package both install gRPCConfig.cmake). Ubuntu 22.04's arm64 archive does
# not ship that file, so there the lookup falls back to the FindgRPC module
# next to this file, which finds the same libraries and defines the same
# imported targets.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
  find_package(gRPC MODULE REQUIRED)
endif()

# ---------------------------------------------------------------------------
# shared-memory transport (Windows implementation lives in the shim tree)
# ---------------------------------------------------------------------------
if(NOT TARGET shm_transport)
  add_subdirectory(${ROS2_BRIDGE_SHM_ROOT} ${CMAKE_BINARY_DIR}/shm_transport)
endif()

# ---------------------------------------------------------------------------
# ros2 shim: header only interface targets mirroring nav2/ros2_shim/CMakeLists
# ---------------------------------------------------------------------------
if(NOT TARGET ros2_messages)
  add_library(ros2_messages INTERFACE)
  target_include_directories(ros2_messages INTERFACE
    ${ROS2_BRIDGE_SHIM_ROOT}/generated/include
    ${ROS2_BRIDGE_SHIM_ROOT}/generated_nav2)
endif()

if(NOT TARGET ros2_shim)
  add_library(ros2_shim INTERFACE)
  target_include_directories(ros2_shim INTERFACE ${ROS2_BRIDGE_SHIM_ROOT}/include)
  target_link_libraries(ros2_shim INTERFACE ros2_messages shm_transport)
endif()

# ---------------------------------------------------------------------------
# gRPC SDK (transport layer). Generated here so the bridge never depends on a
# stale SDK build directory.
# ---------------------------------------------------------------------------
set(ROS2_BRIDGE_PROTO ${ROS2_BRIDGE_SDK_ROOT}/proto/ros2_bridge.proto)
set(ROS2_BRIDGE_PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${ROS2_BRIDGE_PROTO_OUT})

set(ROS2_BRIDGE_GENERATED
  ${ROS2_BRIDGE_PROTO_OUT}/ros2_bridge.pb.cc
  ${ROS2_BRIDGE_PROTO_OUT}/ros2_bridge.pb.h
  ${ROS2_BRIDGE_PROTO_OUT}/ros2_bridge.grpc.pb.cc
  ${ROS2_BRIDGE_PROTO_OUT}/ros2_bridge.grpc.pb.h)

add_custom_command(
  OUTPUT ${ROS2_BRIDGE_GENERATED}
  COMMAND protobuf::protoc
  ARGS --cpp_out=${ROS2_BRIDGE_PROTO_OUT}
       --grpc_out=${ROS2_BRIDGE_PROTO_OUT}
       -I ${ROS2_BRIDGE_SDK_ROOT}/proto
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       ${ROS2_BRIDGE_PROTO}
  DEPENDS ${ROS2_BRIDGE_PROTO} protobuf::protoc gRPC::grpc_cpp_plugin
  COMMENT "Generating ros2_bridge protobuf/gRPC sources"
  VERBATIM)

add_library(ros2_grpc_sdk STATIC
  ${ROS2_BRIDGE_SDK_ROOT}/src/common/status.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/common/log.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/common/version.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/common/cdr.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/common/ros2_types.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/common/yaml.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/common/config.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/client/ros2_bridge_client.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/discovery/discovery.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/server/ros2_bridge_server.cpp
  ${ROS2_BRIDGE_SDK_ROOT}/src/server/mock_backend.cpp
  ${ROS2_BRIDGE_GENERATED})

target_include_directories(ros2_grpc_sdk
  PUBLIC ${ROS2_BRIDGE_SDK_ROOT}/include
  PRIVATE ${ROS2_BRIDGE_PROTO_OUT} ${ROS2_BRIDGE_SDK_ROOT}/src)

target_compile_definitions(ros2_grpc_sdk PRIVATE ROS2_GRPC_SDK_VERSION_STRING="0.1.0")

target_link_libraries(ros2_grpc_sdk PUBLIC Threads::Threads gRPC::grpc++ protobuf::libprotobuf)

if(WIN32)
  target_link_libraries(ros2_grpc_sdk PUBLIC ws2_32 iphlpapi)
endif()

# ---------------------------------------------------------------------------
# The bridge library: CDR <-> shim transcoding table
# ---------------------------------------------------------------------------
add_library(ros2_bridge STATIC
  src/type_registry.cpp
  src/service_registry.cpp
  src/action_registry.cpp)
target_include_directories(ros2_bridge PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${ROS2_BRIDGE_SHIM_ROOT}/generated/include
  ${ROS2_BRIDGE_SHIM_ROOT}/generated_nav2
  ${ROS2_BRIDGE_SHIM_ROOT}/include)
target_link_libraries(ros2_bridge PUBLIC ros2_grpc_sdk ros2_shim)

if(MSVC)
  set(ROS2_BRIDGE_WARN /W4)
else()
  set(ROS2_BRIDGE_WARN -Wall -Wextra)
endif()
target_compile_options(ros2_bridge PRIVATE ${ROS2_BRIDGE_WARN})

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
# MSYS2 ships gRPC, protobuf and abseil as DLLs only, so the daemon cannot be
# linked the fully static way the shim tools are. Copying the DLLs it needs
# next to the executable instead keeps the build directory self contained: the
# Windows loader searches an executable's own directory first, so the daemon
# runs on a machine with no MSYS2 installed and no MSYS2 directory on PATH.
function(ros2_bridge_bundle_runtime target)
  if(NOT WIN32 OR MSVC)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:${target}>
            -DDEST=$<TARGET_FILE_DIR:${target}>
            -DSEARCH=${ROS2_BRIDGE_MSYS2_ROOT}/bin
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_runtime.cmake
    COMMENT "Bundling the MinGW runtime DLLs for ${target}"
    VERBATIM)
endfunction()

add_executable(ros2_bridge_daemon src/bridge_daemon.cpp)
target_link_libraries(ros2_bridge_daemon PRIVATE ros2_bridge)
ros2_bridge_bundle_runtime(ros2_bridge_daemon)

add_executable(ros2 tools/ros2_cli.cpp)
target_link_libraries(ros2 PRIVATE ros2_bridge)

# The visualiser's data plane. It is an ordinary shim node (so the graph sees
# its subscriptions and nav2 keeps publishing /plan and /cmd_vel), plus a
# tf2_ros buffer that transforms every message into the view's fixed frame.
# `nlohmann/json` is the copy the shim already vendors for its own tools.
add_executable(viz_feed tools/viz_feed.cpp)
target_link_libraries(viz_feed PRIVATE ros2_bridge)
target_include_directories(viz_feed PRIVATE
  ${ROS2_BRIDGE_SHIM_ROOT}/../third_party/include)

# The CLI reads the shim graph directly; none of the gRPC half of ros2_bridge
# is reachable from it, so the linker drops that side entirely and the result
# is a fully static executable: `ros2 topic list` runs with no MSYS2 directory
# on PATH and no DLLs beside it.
if(WIN32 AND NOT MSVC)
  target_link_options(ros2 PRIVATE -static -static-libgcc -static-libstdc++)
  # Same reasoning for the sidecar: it is started by a GUI that may not have
  # MSYS2 on PATH, so it must carry its runtime with it.
  target_link_options(viz_feed PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge_grpc_agent.cpp)
  add_executable(ros2_bridge_agent src/bridge_grpc_agent.cpp)
  target_link_libraries(ros2_bridge_agent PRIVATE ros2_bridge)
  ros2_bridge_bundle_runtime(ros2_bridge_agent)
endif()

enable_testing()
set(ROS2_BRIDGE_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/cdr_check.cpp)
  add_executable(bridge_cdr_check tests/cdr_check.cpp)
  target_link_libraries(bridge_cdr_check PRIVATE ros2_bridge)
  target_compile_definitions(bridge_cdr_check PRIVATE
    ROS2_BRIDGE_FIXTURE_DIR="${ROS2_BRIDGE_FIXTURES}")
  add_test(NAME bridge_cdr_roundtrip COMMAND bridge_cdr_check)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/service_roundtrip.cpp)
  add_executable(bridge_service_roundtrip tests/service_roundtrip.cpp)
  target_link_libraries(bridge_service_roundtrip PRIVATE ros2_bridge)
  add_test(NAME bridge_service_roundtrip COMMAND bridge_service_roundtrip)
endif()

# The action protocol the daemon plays back, end to end and without a network:
# a stand-in for the daemon on one side, `rclcpp_action::Client` on the other.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/action_roundtrip.cpp)
  add_executable(bridge_action_roundtrip tests/action_roundtrip.cpp)
  target_link_libraries(bridge_action_roundtrip PRIVATE ros2_bridge)
  add_test(NAME bridge_action_roundtrip COMMAND bridge_action_roundtrip)
endif()

# The command line value syntax of `topic pub` / `service call` /
# `action send_goal`: the YAML document form and the flat `path=value` form
# have to flatten to the same assignments, and a value that cannot be
# represented has to be an error instead of a different message.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_values.cpp)
  add_executable(bridge_cli_values tests/cli_values.cpp)
  target_link_libraries(bridge_cli_values PRIVATE ros2_bridge)
  add_test(NAME bridge_cli_values COMMAND bridge_cli_values)
endif()

# The contract between this tree and the shim's channels, as
# `ros2_bridge/shm_topic.hpp` states it: the names, the slot sizes, and a
# publish/drain round trip through two handles. No daemon, no network and
# no fixtures, so it runs anywhere the other four do.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/shm_roundtrip.cpp)
  add_executable(bridge_shm_roundtrip tests/shm_roundtrip.cpp)
  target_link_libraries(bridge_shm_roundtrip PRIVATE ros2_bridge)
  add_test(NAME bridge_shm_roundtrip COMMAND bridge_shm_roundtrip)
endif()

# The sidecar's JSON contract, against synthetic messages and no shared memory:
# a map's base64 payload, a scan's point list, and the quaternion-to-yaw path
# must all survive a build with no GUI and no running stack.
add_test(NAME bridge_viz_feed_selftest COMMAND viz_feed --selftest)
set_tests_properties(bridge_viz_feed_selftest PROPERTIES
  PASS_REGULAR_EXPRESSION "\"ok\":true" TIMEOUT 30)

# Every test in this tree that talks to the shim writes into the same graph and
# the same shared-memory channels the bridge daemon uses, and a channel has a
# single writer by contract. `bridge_action_roundtrip` and
# `bridge_service_roundtrip` both register nodes, services and an action, so
# with the bridge up their records landed in the live graph: `ros2 action list`
# showed a `/test/drive` that belongs to no running stack, `ros2 node list`
# showed `/action_roundtrip`, and the six `/action_roundtrip/*` parameter
# services came with it. Measured against a live `ros2_bridge_daemon`: twelve
# un-isolated runs produced all of those, the same twelve runs with
# `NAV2_SHM_NAMESPACE` set produced none. Each test gets a namespace of its own,
# the way `nav2/ros2_shim/CMakeLists.txt` already does it for its 26. The
# `nav2/shm_transport` test is added as a subdirectory and sets its own.
get_property(BRIDGE_TEST_NAMES DIRECTORY PROPERTY TESTS)
foreach(_test IN LISTS BRIDGE_TEST_NAMES)
  set_tests_properties(${_test} PROPERTIES
    ENVIRONMENT_MODIFICATION "NAV2_SHM_NAMESPACE=set:ctest_${_test}"
    # These tests wait on shared-memory channels too, so they hang the same way
    # a killed writer makes `shm_transport` hang - see the note on that test in
    # `nav2/shm_transport/CMakeLists.txt`.
    TIMEOUT 300)
endforeach()