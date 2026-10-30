cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# slam_toolbox on Windows: the parts upstream's CMake cannot build here.
#
#   kartoSlamToolbox            - the Karto front-end slam_toolbox vendors
#   slam_toolbox_solver_plugins - pose-graph solver replacing CeresSolver
#
# The slam_toolbox node sources themselves are compiled unmodified, right here.
# ---------------------------------------------------------------------------

set(SLAM_TOOLBOX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/slam_toolbox)
set(KARTO_SRC ${SLAM_TOOLBOX_SRC}/lib/karto_sdk)
set(SHIM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../ros2_shim/include)

# ---------------------------------------------------------------------------
# The two Karto files below are vendored upstream code (slam_toolbox 2.8.5) that
# this port has to edit, and re-vendoring upstream drops the edits silently.
#
# `serialize_map` writes a `boost::archive::binary_oarchive` - arbitrary bytes -
# and `deserialize_map` reads one back. Both streams are opened in *text* mode
# upstream, which on Windows turns the first 0x1A byte into end of file: the
# archive comes back as `archive_exception: input stream error` and the service
# answers `false` with nothing in the log naming the cause. Linux cannot show
# this, so it is exactly the kind of edit that disappears without anything
# failing at build time.
#
# What is checked here is the result - the two lines in each file - rather than
# the patch: a re-vendored tree fails configure instead of producing an exe that
# only breaks when somebody loads a map. The edits, why they are needed, and how
# to re-verify after an upgrade are in nav2/patches/README.md.
# ---------------------------------------------------------------------------
foreach(_karto_file
    ${KARTO_SRC}/src/Mapper.cpp
    ${KARTO_SRC}/include/karto_sdk/Karto.h)
  file(READ ${_karto_file} _karto_text)
  # One `std::ios::binary` and one marker per save/load pair, twice in each file.
  string(REGEX MATCHALL "std::ios::binary" _karto_binary "${_karto_text}")
  string(REGEX MATCHALL "PORT \\(ros2_shim\\)" _karto_markers "${_karto_text}")
  list(LENGTH _karto_binary _karto_binary_count)
  list(LENGTH _karto_markers _karto_marker_count)
  if(NOT _karto_binary_count EQUAL 2 OR NOT _karto_marker_count EQUAL 2)
    message(FATAL_ERROR
      "${_karto_file} has lost a ros2_shim port edit: expected two uses of "
      "std::ios::binary and two PORT (ros2_shim) markers, found "
      "${_karto_binary_count} and ${_karto_marker_count}. Without them a "
      "text-mode stream ends at the first 0x1A byte and deserialize_map fails "
      "with archive_exception: input stream error. Re-apply "
      "nav2/patches/slam_toolbox-ucrt-binary-archive.patch - see "
      "nav2/patches/README.md.")
  endif()
endforeach()

# The third edit is not a Karto one, and it is the reason this section earns
# its keep: upstream's `SynchronousSlamToolbox` destructor is empty, so the base
# destructor interrupts and joins `run()` only *after* `q_mutex_` - a member of
# the derived class - has been destroyed. On glibc the next lock of that mutex
# returns EINVAL, boost turns it into `lock_error`, and the process aborts;
# Windows' pthread implementation does not validate the kind, so nothing fails
# here and a re-vendor would drop the edit silently. Measured on Ubuntu 24.04:
# 3 runs out of 3 died at the first `slam.reset()` of a mapping lap.
file(READ ${SLAM_TOOLBOX_SRC}/include/slam_toolbox/slam_toolbox_sync.hpp _sync_text)
string(REGEX MATCHALL "PORT \\(ros2_shim\\)" _sync_markers "${_sync_text}")
string(REGEX MATCHALL "thread->join\\(\\)" _sync_joins "${_sync_text}")
list(LENGTH _sync_markers _sync_marker_count)
list(LENGTH _sync_joins _sync_join_count)
if(NOT _sync_marker_count EQUAL 1 OR NOT _sync_join_count EQUAL 1)
  message(FATAL_ERROR
    "third_party/slam_toolbox/include/slam_toolbox/slam_toolbox_sync.hpp has "
    "lost the ros2_shim port edit to ~SynchronousSlamToolbox: expected one "
    "PORT (ros2_shim) marker and one thread->join(), found "
    "${_sync_marker_count} and ${_sync_join_count}. Without it the base "
    "destructor joins a thread that keeps locking a mutex the derived class "
    "already destroyed, which aborts the process on Linux. Re-apply "
    "nav2/patches/slam_toolbox-thread-shutdown.patch - see "
    "nav2/patches/README.md.")
endif()

find_package(Eigen3 CONFIG REQUIRED)

# Boost has to be static: the goal is executables that run with no MSYS2 UCRT64
# bin directory on PATH.
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost CONFIG REQUIRED COMPONENTS serialization thread)

# Karto is built as a static library, so KARTO_EXPORT has to expand to nothing;
# otherwise Macros.h falls back to __declspec(dllimport) and every definition in
# Karto.cpp is flagged as a mismatched redeclaration.
add_library(kartoSlamToolbox STATIC
  ${KARTO_SRC}/src/Karto.cpp
  ${KARTO_SRC}/src/Mapper.cpp)
target_compile_definitions(kartoSlamToolbox PUBLIC KARTO_STATIC)
target_include_directories(kartoSlamToolbox PUBLIC
  ${KARTO_SRC}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(kartoSlamToolbox PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/karto_compat/uint.hpp)
# Karto.h declares ScanSolver::Configure(rclcpp_lifecycle::LifecycleNode::SharedPtr),
# so its headers need the shim (and the generated messages underneath it).
target_link_libraries(kartoSlamToolbox PUBLIC
  ros2_shim
  Eigen3::Eigen
  Boost::serialization
  Boost::thread)

add_library(slam_toolbox_solver_plugins STATIC
  src/eigen_pose_graph_solver.cpp)
target_include_directories(slam_toolbox_solver_plugins PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHIM_INCLUDE})
target_link_libraries(slam_toolbox_solver_plugins PUBLIC
  ros2_shim
  kartoSlamToolbox)
target_compile_options(slam_toolbox_solver_plugins PRIVATE
  -include ${SHIM_INCLUDE}/ros2_shim/prelude.hpp)

add_executable(test_eigen_pose_graph_solver tests/test_eigen_pose_graph_solver.cpp)
target_link_libraries(test_eigen_pose_graph_solver PRIVATE slam_toolbox_solver_plugins)

# ---------------------------------------------------------------------------
# The signed-off slam_toolbox distribution, compiled unmodified.
#
# Excluded on purpose:
#   rviz_plugin/      - Qt, OGRE and rviz_common; there is no RViz here.
#   solvers/ceres_*   - Ceres only ships as a DLL on MSYS2, which would defeat
#                       the static self-contained binary goal; the port's own
#                       Eigen solver takes its place under the plugin name
#                       `solver_plugins::CeresSolver` that the shipped config
#                       files already reference.
#   src/experimental/ - lifelong mapping additionally drags in interactive
#                       markers state that nothing here consumes.
# ---------------------------------------------------------------------------
set(SLAM_TOOLBOX_INCLUDE ${SLAM_TOOLBOX_SRC}/include)

function(slam_toolbox_target target)
  target_include_directories(${target} PRIVATE ${SLAM_TOOLBOX_INCLUDE})
  target_compile_options(${target} PRIVATE
    -include ${SHIM_INCLUDE}/ros2_shim/prelude.hpp)
endfunction()

# The solver plugin announces itself to the shim's pluginlib registry from a
# static initialiser, and nothing ever names it by symbol -- so a plain archive
# link drops the object file and `setSolver()` then fails with "no registered
# plugin matching solver_plugins::CeresSolver". Whole-archive keeps it.
function(slam_toolbox_link_solver_registry target)
  if(MSVC)
    target_link_options(${target} PRIVATE
      "/WHOLEARCHIVE:$<TARGET_FILE:slam_toolbox_solver_plugins>")
  else()
    target_link_options(${target} PRIVATE
      "-Wl,--whole-archive" "$<TARGET_FILE:slam_toolbox_solver_plugins>"
      "-Wl,--no-whole-archive")
  endif()
endfunction()

add_library(slam_toolbox_common STATIC
  ${SLAM_TOOLBOX_SRC}/src/slam_toolbox_common.cpp
  ${SLAM_TOOLBOX_SRC}/src/map_saver.cpp
  ${SLAM_TOOLBOX_SRC}/src/loop_closure_assistant.cpp
  ${SLAM_TOOLBOX_SRC}/src/laser_utils.cpp
  ${SLAM_TOOLBOX_SRC}/src/slam_mapper.cpp)
slam_toolbox_target(slam_toolbox_common)
target_link_libraries(slam_toolbox_common PUBLIC
  kartoSlamToolbox
  slam_toolbox_solver_plugins
  Boost::thread)

add_library(slam_toolbox_async STATIC ${SLAM_TOOLBOX_SRC}/src/slam_toolbox_async.cpp)
slam_toolbox_target(slam_toolbox_async)
target_link_libraries(slam_toolbox_async PUBLIC slam_toolbox_common)
add_executable(async_slam_toolbox_node ${SLAM_TOOLBOX_SRC}/src/slam_toolbox_async_node.cpp)
slam_toolbox_target(async_slam_toolbox_node)
target_link_libraries(async_slam_toolbox_node PRIVATE slam_toolbox_async)
slam_toolbox_link_solver_registry(async_slam_toolbox_node)

add_library(slam_toolbox_sync STATIC ${SLAM_TOOLBOX_SRC}/src/slam_toolbox_sync.cpp)
slam_toolbox_target(slam_toolbox_sync)
target_link_libraries(slam_toolbox_sync PUBLIC slam_toolbox_common)
add_executable(sync_slam_toolbox_node ${SLAM_TOOLBOX_SRC}/src/slam_toolbox_sync_node.cpp)
slam_toolbox_target(sync_slam_toolbox_node)
target_link_libraries(sync_slam_toolbox_node PRIVATE slam_toolbox_sync)
slam_toolbox_link_solver_registry(sync_slam_toolbox_node)

add_library(slam_toolbox_localization STATIC
  ${SLAM_TOOLBOX_SRC}/src/slam_toolbox_localization.cpp)
slam_toolbox_target(slam_toolbox_localization)
target_link_libraries(slam_toolbox_localization PUBLIC slam_toolbox_common)
add_executable(localization_slam_toolbox_node
  ${SLAM_TOOLBOX_SRC}/src/slam_toolbox_localization_node.cpp)
slam_toolbox_target(localization_slam_toolbox_node)
target_link_libraries(localization_slam_toolbox_node PRIVATE slam_toolbox_localization)
slam_toolbox_link_solver_registry(localization_slam_toolbox_node)

add_executable(merge_maps_kinematic ${SLAM_TOOLBOX_SRC}/src/merge_maps_kinematic.cpp)
slam_toolbox_target(merge_maps_kinematic)
target_link_libraries(merge_maps_kinematic PRIVATE slam_toolbox_common)

# Runs a slam_toolbox node against the shared-memory graph, which is what makes
# SLAM usable from the bridge: /scan, /odom and /tf arrive from Ubuntu over
# gRPC and the resulting map is published locally. See apps/slam_bridge_node.cpp
# for why the lifecycle transitions happen in here.
add_executable(slam_bridge_node apps/slam_bridge_node.cpp)
slam_toolbox_target(slam_bridge_node)
target_link_libraries(slam_bridge_node PRIVATE
  slam_toolbox_sync
  slam_toolbox_async
  slam_toolbox_localization
  ros2_shim_params)
slam_toolbox_link_solver_registry(slam_bridge_node)

# Drives the node above over shared memory with a synthetic robot and laser.
add_executable(test_slam_mapping tests/test_slam_mapping.cpp)
slam_toolbox_target(test_slam_mapping)
target_link_libraries(test_slam_mapping PRIVATE slam_toolbox_sync)
slam_toolbox_link_solver_registry(test_slam_mapping)

# The pose-graph archive: `slam_toolbox/serialize_map` writes a `.posegraph` +
# `.data` pair, a second node reads them back with `deserialize_map` and has to
# publish the same grid. The test's header says why it is a round trip rather
# than a call that merely answers.
add_executable(test_slam_serialization tests/test_slam_serialization.cpp)
slam_toolbox_target(test_slam_serialization)
target_link_libraries(test_slam_serialization PRIVATE slam_toolbox_sync)
slam_toolbox_link_solver_registry(test_slam_serialization)

# Karto's own laser model: checks where a scan's points land, which is what
# the occupancy grid is rasterized from.
add_executable(test_laser_points tests/test_laser_points.cpp)
slam_toolbox_target(test_laser_points)
target_link_libraries(test_laser_points PRIVATE kartoSlamToolbox)
