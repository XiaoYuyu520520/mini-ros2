cmake_minimum_required(VERSION 3.16)
project(ros2_shim C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Static linking
#
# Everything the shim builds on Windows has to be a self-contained program: it
# must not need the MSYS2 UCRT64 `bin` directory (libstdc++-6.dll,
# libgcc_s_seh-1.dll, libwinpthread-1.dll) on PATH at run time. `-static` pulls
# the GNU runtime into the executable, leaving only the Windows system DLLs
# (KERNEL32, api-ms-win-crt-*) dynamic.
#
# MinGW only. The same flags under glibc mean "statically link libc too", which
# needs a libc.a most distributions do not install and buys nothing here: a
# Linux build is expected to use the system runtime. `-static-libstdc++` is a
# GCC-under-Linux flag too, but it is the libc one that breaks the link, so the
# whole block is Windows-only rather than half of it.
# ---------------------------------------------------------------------------
option(NAV2_SHIM_STATIC "Statically link the GNU runtime into shim binaries" ON)
if(NAV2_SHIM_STATIC AND MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

add_subdirectory(
  ${CMAKE_CURRENT_SOURCE_DIR}/../shm_transport
  ${CMAKE_CURRENT_BINARY_DIR}/shm_transport)

# Generated ROS2 message structs (mirror ROS2 include paths).
add_library(ros2_messages INTERFACE)
target_include_directories(ros2_messages INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/generated/include)

# rclcpp-compatible shim headers.
add_library(ros2_shim INTERFACE)
target_include_directories(ros2_shim INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ros2_shim INTERFACE ros2_messages shm_transport)

# ---- parameter files -------------------------------------------------------
# `--params-file` support: yaml-cpp built from the vendored source, plus the
# loader that the Windows-side runners and the nav2 servers share. It is
# defined up here because the slam_toolbox subdirectory below already defines
# the first runner that links it.
set(YAML_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/dl/yamlcpp/yaml-cpp-0.8.0)
file(GLOB_RECURSE YAML_SRC ${YAML_ROOT}/src/*.cpp)
add_library(yaml-cpp STATIC ${YAML_SRC})
target_include_directories(yaml-cpp PUBLIC ${YAML_ROOT}/include)
target_compile_definitions(yaml-cpp PUBLIC YAML_CPP_STATIC_DEFINE)
target_compile_options(yaml-cpp PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

add_library(ros2_shim_params STATIC src/params_file.cpp)
target_include_directories(ros2_shim_params PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ros2_shim_params PUBLIC ros2_shim yaml-cpp)
target_compile_options(ros2_shim_params PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

# ---- slam_toolbox ----------------------------------------------------------
# Karto plus the pose-graph solver plugin that replaces upstream's CeresSolver.
# The slam_toolbox node sources are compiled further below.
add_subdirectory(
  ${CMAKE_CURRENT_SOURCE_DIR}/../slam_toolbox_port
  ${CMAKE_CURRENT_BINARY_DIR}/slam_toolbox_port)

add_executable(test_messages tests/test_messages.cpp)
target_link_libraries(test_messages PRIVATE ros2_shim)

# Compile-check every generated message header.
add_executable(compile_check_messages tests/compile_all_messages.cpp)
target_link_libraries(compile_check_messages PRIVATE ros2_messages)

# Cross-language interop harness (C++ side).
add_executable(py_interop tests/py_interop.cpp)
target_link_libraries(py_interop PRIVATE ros2_shim)

# Live diagnostic (not a ctest): prints what a shim node's subscriptions
# actually receive from the bridge. Run it by hand with the daemon up.
add_executable(tf_probe tests/tf_probe.cpp)
target_link_libraries(tf_probe PRIVATE ros2_shim)

# Live diagnostic (not a ctest): dumps an OccupancyGrid from the shim graph to
# a PGM and an ASCII preview. `ros2 topic echo` truncates the grid, so this is
# how a bridged map gets eyeballed.
add_executable(map_dump tests/map_dump.cpp)
target_link_libraries(map_dump PRIVATE ros2_shim)

add_executable(test_shim_node tests/test_shim_node.cpp)
target_link_libraries(test_shim_node PRIVATE ros2_shim)

add_executable(test_shim_compile tests/test_shim_compile.cpp)
target_link_libraries(test_shim_compile PRIVATE ros2_shim)

set(NAV2_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../nav2_extracted/navigation2-1.5.1)

# Every `.cpp` that belongs in a package's library, minus the ones that are
# standalone programs. A package's own `main` - upstream builds `planner_server`
# from nav2_planner/src/main.cpp, the costmap visualisers from
# nav2_costmap_2d/src/costmap_2d_{node,cloud,markers}.cpp, and so on - has no
# place in a static library the shim links: the shim's servers are built from
# apps/ and take the same parameters from the command line, and a second `main`
# in the archive collides with theirs the moment the archive is whole-archived
# for the plugins it registers.
function(nav2_package_sources out_var pkg_dir)
  file(GLOB_RECURSE _candidates
    ${NAV2_SRC}/${pkg_dir}/src/*.cpp
    ${NAV2_SRC}/${pkg_dir}/plugins/*.cpp)
  set(_sources "")
  foreach(_candidate ${_candidates})
    file(READ ${_candidate} _text)
    if(NOT _text MATCHES "(^|\n)[ \t]*(int|void)[ \t]+main[ \t]*\\(")
      list(APPEND _sources ${_candidate})
    endif()
  endforeach()
  set(${out_var} ${_sources} PARENT_SCOPE)
endfunction()

add_executable(test_nav2_ros_common tests/test_nav2_ros_common.cpp)
target_link_libraries(test_nav2_ros_common PRIVATE ros2_shim)
target_include_directories(test_nav2_ros_common PRIVATE
  ${NAV2_SRC}/nav2_ros_common/include)

# Live diagnostic (not a ctest): reproduces nav2_amcl's transform setup against
# the bridged /tf so a missing frame can be traced to one construction step.
add_executable(tf_listener_probe tests/tf_listener_probe.cpp)
target_link_libraries(tf_listener_probe PRIVATE ros2_shim)
target_include_directories(tf_listener_probe PRIVATE
  ${NAV2_SRC}/nav2_ros_common/include)

# Live diagnostic (not a ctest): watches nav2_amcl's `map` -> `odom` output
# against the bridged `/odom`, both at the newest sample and at the instant
# AMCL stamped its estimate, so a stale estimate is not read as a bad one.
add_executable(amcl_probe tests/amcl_probe.cpp)
target_link_libraries(amcl_probe PRIVATE ros2_shim)

# ---- navigation2 packages under the shared-memory shim --------------------
set(NAV2_TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/include)
file(GLOB NAV2_INCLUDE_DIRS
  ${NAV2_SRC}/*/include
  ${NAV2_SRC}/*/*/include)
list(APPEND NAV2_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/generated_nav2)

# Sources named after the package directory are dropped from the library: a
# plugin that needs a dependency this toolchain does not ship is left out
# rather than stubbed, so the package keeps its other plugins.
function(nav2_port_package target pkg_dir)
  nav2_package_sources(_sources ${pkg_dir})
  foreach(_excluded ${ARGN})
    list(REMOVE_ITEM _sources ${NAV2_SRC}/${pkg_dir}/${_excluded})
  endforeach()
  add_library(${target} STATIC ${_sources})
  target_include_directories(${target} PUBLIC
    ${NAV2_SRC}/${pkg_dir}/include
    ${NAV2_INCLUDE_DIRS}
    ${NAV2_TP})
  target_link_libraries(${target} PUBLIC ros2_shim)
  target_compile_options(${target} PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
endfunction()

add_library(nav2_voxel_grid STATIC
  ${NAV2_SRC}/nav2_voxel_grid/src/voxel_grid.cpp)
target_include_directories(nav2_voxel_grid PUBLIC
  ${NAV2_SRC}/nav2_voxel_grid/include ${NAV2_INCLUDE_DIRS} ${NAV2_TP})
target_link_libraries(nav2_voxel_grid PUBLIC ros2_shim)
target_compile_options(nav2_voxel_grid PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

nav2_package_sources(NAV2_UTIL_SOURCES nav2_util)
add_library(nav2_util_core STATIC ${NAV2_UTIL_SOURCES})
target_include_directories(nav2_util_core PUBLIC
  ${NAV2_SRC}/nav2_util/include
  ${NAV2_SRC}/nav2_ros_common/include
  ${NAV2_INCLUDE_DIRS}
  ${NAV2_TP})
target_link_libraries(nav2_util_core PUBLIC ros2_shim nav2_voxel_grid)
target_compile_options(nav2_util_core PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

# nav2_costmap_2d's obstacle and voxel layers project every scan through
# `laser_geometry`, which upstream pulls in as its own package. Only the two
# entry points those layers call are implemented (third_party/laser_geometry),
# against the shim's tf2 and sensor_msgs.
add_library(laser_geometry STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/laser_geometry/src/laser_geometry.cpp)
target_include_directories(laser_geometry PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/laser_geometry/include
  ${NAV2_INCLUDE_DIRS} ${NAV2_TP})
target_link_libraries(laser_geometry PUBLIC ros2_shim)
target_compile_options(laser_geometry PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

# Smac's Hybrid-A* asks OMPL for the shortest Dubins or Reeds-Shepp path between
# two poses, and uses it both as its heuristic and to finish a search
# analytically; the state lattice asks the same of SE(2). Only that much of OMPL
# is implemented, in third_party/ompl - the rest of OMPL is not ported, and
# third_party/ompl/README.md says what that costs.
add_library(ompl_shim STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ompl/src/dubins.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ompl/src/reeds_shepp.cpp)
target_include_directories(ompl_shim PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ompl/include)
target_compile_options(ompl_shim PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

nav2_port_package(nav2_costmap_2d nav2_costmap_2d)
target_link_libraries(nav2_costmap_2d PUBLIC
  nav2_util_core nav2_voxel_grid laser_geometry)

# ---- BehaviorTree.CPP (built externally) ----------------------------------
set(BTCPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/dl/btcpp_master/BehaviorTree.CPP-master)
set(BTCPP_BUILD ${BTCPP_ROOT}/build)
add_library(btcpp_headers INTERFACE)
target_include_directories(btcpp_headers INTERFACE
  ${BTCPP_ROOT}/include
  ${BTCPP_ROOT}/3rdparty
  ${BTCPP_ROOT}/3rdparty/tinyxml2
  ${NAV2_TP})
add_library(behaviortree_cpp STATIC IMPORTED)
set_target_properties(behaviortree_cpp PROPERTIES
  IMPORTED_LOCATION ${BTCPP_BUILD}/libbehaviortree_cpp.a)
add_library(btcpp_minitrace STATIC IMPORTED)
set_target_properties(btcpp_minitrace PROPERTIES
  IMPORTED_LOCATION ${BTCPP_BUILD}/3rdparty/minitrace/libminitrace.a)
add_library(btcpp_tinyxml2 STATIC IMPORTED)
set_target_properties(btcpp_tinyxml2 PROPERTIES
  IMPORTED_LOCATION ${BTCPP_BUILD}/3rdparty/tinyxml2/libtinyxml2.a)
# The vendored BT.CPP is built without BTCPP_GROOT_INTERFACE (ZeroMQ, see
# src/btcpp_groot_stub.cpp), so the Groot2 status logger that
# nav2_behavior_tree's engine still names has no definition anywhere.
add_library(btcpp_groot_stub STATIC src/btcpp_groot_stub.cpp)
target_include_directories(btcpp_groot_stub PRIVATE
  ${BTCPP_ROOT}/include
  ${BTCPP_ROOT}/3rdparty
  ${NAV2_TP})
target_link_libraries(btcpp_groot_stub PUBLIC btcpp_headers)
target_compile_options(btcpp_groot_stub PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

target_link_libraries(behaviortree_cpp INTERFACE
  btcpp_headers btcpp_minitrace btcpp_tinyxml2 btcpp_groot_stub)

# generate_nav2_tree_nodes_xml.cpp is a build-time tool that writes the list of
# built-in BT nodes; nav2_package_sources drops it along with the other `main`s.
nav2_package_sources(NAV2_BT_SOURCES nav2_behavior_tree)

# ---- nav2_behavior_tree: every node has to register itself ------------------
# Upstream builds each node in `plugins/` as its own shared library and points
# BT.CPP's factory at it (`registerFromPlugin`), which calls the library's
# `BT_RegisterNodesFromPlugin()`. That function comes from `BT_REGISTER_NODES`,
# and BT.CPP expands the macro to a *static* function unless `BT_PLUGIN_EXPORT`
# is defined (bt_factory.h). In a static library nothing can reach it, so the
# nodes never enter the factory and every tree that names one dies while it is
# being loaded:
#
#   BT::RuntimeError: Error at line 8: -> Node not recognized: RecoveryNode
#
# So each node is compiled through a generated wrapper that renames its
# registration function in the preprocessor and re-exports it under a unique
# name, plus one generated registry that calls them all. `BehaviorTreeEngine`'s
# constructor invokes the registry - see the PORT note in
# nav2_behavior_tree/src/behavior_tree_engine.cpp. The nodes are filtered out of
# `nav2_package_sources`' list so that each of them is still compiled once.
file(GLOB_RECURSE NAV2_BT_NODES ${NAV2_SRC}/nav2_behavior_tree/plugins/*.cpp)
list(FILTER NAV2_BT_SOURCES EXCLUDE REGEX "(^|/)plugins/")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bt_node_registry)

set(NAV2_BT_NODE_WRAPPERS "")
set(_bt_registry_declarations "")
set(_bt_registry_calls "")
foreach(_node ${NAV2_BT_NODES})
  file(RELATIVE_PATH _node_rel ${NAV2_SRC}/nav2_behavior_tree/plugins ${_node})
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" _node_id "${_node_rel}")
  set(_wrapper ${CMAKE_CURRENT_BINARY_DIR}/bt_node_registry/${_node_id}.cpp)
  set(_wrapper_text "// Generated by ros2_shim/CMakeLists.txt - do not edit.\n")
  string(APPEND _wrapper_text
    "// `BT_REGISTER_NODES` defines a *static* function, so the node's own\n"
    "// translation unit is the only place that can hand it to the factory.\n"
    "#define BT_RegisterNodesFromPlugin nav2_shim_bt_register_${_node_id}\n"
    "#include \"${_node}\"\n"
    "#undef BT_RegisterNodesFromPlugin\n"
    "\n"
    "namespace nav2_shim\n{\n"
    "namespace bt_registry\n{\n"
    "void ${_node_id}(BT::BehaviorTreeFactory & factory)\n"
    "{\n"
    "  nav2_shim_bt_register_${_node_id}(factory);\n"
    "}\n"
    "}  // namespace bt_registry\n"
    "}  // namespace nav2_shim\n")
  file(GENERATE OUTPUT ${_wrapper} CONTENT "${_wrapper_text}")
  list(APPEND NAV2_BT_NODE_WRAPPERS ${_wrapper})
  string(APPEND _bt_registry_declarations
    "void ${_node_id}(BT::BehaviorTreeFactory & factory);\n")
  string(APPEND _bt_registry_calls "  ${_node_id}(factory);\n")
endforeach()

set(_bt_registry_text "// Generated by ros2_shim/CMakeLists.txt - do not edit.\n")
string(APPEND _bt_registry_text
  "#include \"ros2_shim/bt_node_registry.hpp\"\n"
  "\n"
  "namespace nav2_shim\n{\n"
  "namespace bt_registry\n{\n"
  "${_bt_registry_declarations}"
  "\n"
  "void registerNodes(BT::BehaviorTreeFactory & factory)\n"
  "{\n"
  "${_bt_registry_calls}"
  "}\n"
  "\n"
  "}  // namespace bt_registry\n"
  "}  // namespace nav2_shim\n")
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bt_node_registry/registry.cpp
  CONTENT "${_bt_registry_text}")

add_library(nav2_behavior_tree STATIC ${NAV2_BT_SOURCES} ${NAV2_BT_NODE_WRAPPERS}
  ${CMAKE_CURRENT_BINARY_DIR}/bt_node_registry/registry.cpp)
target_include_directories(nav2_behavior_tree PUBLIC
  ${NAV2_SRC}/nav2_behavior_tree/include
  ${NAV2_SRC}/nav2_behavior_tree/include/nav2_behavior_tree
  ${NAV2_INCLUDE_DIRS}
  ${NAV2_TP})
target_compile_options(nav2_behavior_tree PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(nav2_behavior_tree PUBLIC
  behaviortree_cpp nav2_util_core nav2_costmap_2d)

nav2_port_package(nav2_navfn_planner nav2_navfn_planner)
target_link_libraries(nav2_navfn_planner PUBLIC
  nav2_util_core nav2_costmap_2d nav2_behavior_tree)

# Grid A* (SmacPlanner2D), Hybrid-A* (SmacPlannerHybrid) and the state lattice
# (SmacPlannerLattice). The lattice reads its motion primitives from a JSON file
# named by `lattice_filepath`, which is why nlohmann/json is in the include path
# already; the other two need no such file and no nlohmann at all.
nav2_port_package(nav2_smac_planner nav2_smac_planner)
target_link_libraries(nav2_smac_planner PUBLIC
  nav2_util_core nav2_costmap_2d ompl_shim)

# ---- end-to-end demo: Ubuntu topics -> nav2 planning on Windows -> /cmd_vel -
# Runs against a live bridge (see bridge/windows/nav2_demo.bat), so it is not a
# ctest: it subscribes to the mirrored /map, /scan and /odom, plans with the
# ported NavFn and drives the remote robot back through the gRPC daemon.
add_executable(nav2_bridge_demo apps/nav2_bridge_demo.cpp)
target_compile_options(nav2_bridge_demo PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(nav2_bridge_demo PRIVATE ros2_shim nav2_navfn_planner)

add_executable(test_navfn_planning tests/navfn_planning.cpp)
target_compile_options(test_navfn_planning PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_navfn_planning PRIVATE ros2_shim nav2_navfn_planner)

# `--params-file`: the config files that ship with slam_toolbox and nav2 have to
# land as the parameters the ported nodes declare, wildcards and all.
add_executable(test_params_file tests/params_file.cpp)
target_compile_options(test_params_file PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_params_file PRIVATE ros2_shim_params)
target_compile_definitions(test_params_file PRIVATE
  PARAMS_TEST_SLAM_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../third_party/slam_toolbox/config/mapper_params_online_async.yaml"
  PARAMS_TEST_NAV2_PARAMS="${NAV2_SRC}/nav2_bringup/params/nav2_params.yaml"
  PARAMS_TEST_MAPPER_LOCALIZATION="${CMAKE_CURRENT_SOURCE_DIR}/../third_party/slam_toolbox/config/mapper_params_localization.yaml")

# The transform buffer is time-aware: a lookup at an instant between two
# broadcasts has to interpolate, and only the links below the deepest shared
# ancestor may be consulted (see tests/tf_time.cpp).
add_executable(test_tf_time tests/tf_time.cpp)
target_compile_options(test_tf_time PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_tf_time PRIVATE ros2_shim)

# `tf2::convert` has to copy when both sides are the same type -- the costmap's
# sensor origin goes through that path, and a conversion that silently does
# nothing leaves the costmap raytracing from (0, 0, 0) (see tests/tf_convert.cpp).
add_executable(test_tf_convert tests/tf_convert.cpp)
target_compile_options(test_tf_convert PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_tf_convert PRIVATE ros2_shim)

# The same thing for a body that is *moving*: every stamped lookup has to come
# back as the pose broadcast for that stamp, and consecutive poses have to stay
# a full step apart, or slam_toolbox's minimum-travel gate quietly drops every
# other scan and the map it builds is the wrong shape (see
# tests/tf_buffer_lookup.cpp).
add_executable(test_tf_buffer_lookup tests/tf_buffer_lookup.cpp)
target_compile_options(test_tf_buffer_lookup PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_tf_buffer_lookup PRIVATE ros2_shim)

# Live diagnostic (not a ctest): what a scan sees between the publisher, the
# tf2_ros::MessageFilter and the odom pose the buffer answers at its stamp.
add_executable(scan_path_probe tests/scan_path_probe.cpp)
target_compile_options(scan_path_probe PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(scan_path_probe PRIVATE ros2_shim)

add_executable(test_e2e tests/test_e2e.cpp)
target_compile_options(test_e2e PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_e2e PRIVATE
  ros2_shim nav2_navfn_planner nav2_costmap_2d nav2_util_core)

nav2_port_package(nav2_planner nav2_planner)
target_link_libraries(nav2_planner PUBLIC
  nav2_util_core nav2_costmap_2d nav2_behavior_tree nav2_navfn_planner)

nav2_port_package(nav2_controller nav2_controller)
target_link_libraries(nav2_controller PUBLIC
  nav2_util_core nav2_costmap_2d nav2_behavior_tree)

nav2_port_package(nav2_regulated_pure_pursuit_controller nav2_regulated_pure_pursuit_controller)
target_link_libraries(nav2_regulated_pure_pursuit_controller PUBLIC
  nav2_util_core nav2_costmap_2d)

# The rotation shim: a controller that is itself a plugin and loads another
# one. It turns the robot in place towards the path before handing over, which
# is the piece RPP alone does not do - RPP starts following the path from
# wherever the robot happens to be pointing.
#
# It reaches for nothing the shim does not already have (`angles`,
# `nav2_costmap_2d/footprint_collision_checker`, `nav2_util`), and it is a
# plugin like any other, so controller_server has to whole-archive it - see the
# nav2_server(controller_server ...) line below.
nav2_port_package(nav2_rotation_shim_controller nav2_rotation_shim_controller)
target_link_libraries(nav2_rotation_shim_controller PUBLIC
  nav2_util_core nav2_costmap_2d)

# ---- DWB (nav2_dwb_controller) ---------------------------------------------
# The second controller family, and the one that turns this stack's controller
# list into an actual choice. DWB is a *sampling* controller - it rolls out a
# set of candidate trajectories and scores them with the critics named in the
# parameters file - where RPP and the rotation shim are path followers.
#
# Upstream splits it across four libraries, and the split is kept here because
# it is what the dependency edges actually are: the critics and the trajectory
# generator only talk to `dwb_core`'s interfaces, and `dwb_core` only talks to
# `nav2_core`'s.
#
# `costmap_queue` is DWB's own breadth-first walk over the costmap, used by
# `MapGridCritic` to ask "how far is this pose from an obstacle"; `nav_2d_utils`
# is the `nav_msgs/Path` <-> `nav_2d_msgs/Path2D` conversion DWB works in, and
# both `nav_2d_msgs` and `dwb_msgs` are already among the generated message
# headers (see tools/generate_messages.py).
#
# That last one is a link dependency, not just an include: `dwb_core` calls
# `nav_2d_utils::twist3Dto2D`, `twist2Dto3D` and `posesToPath`, and the critics
# call `adjustPlanResolution`. Upstream's dwb_core package.xml lists nav_2d_utils
# so the edge is transitive there, but here each library names its own, and
# leaving it off is not a compile error - it is an `undefined reference` only
# once controller_server links the whole archive, because a plain archive link
# of dwb_core alone would never have pulled the calling objects in either.
nav2_port_package(costmap_queue nav2_dwb_controller/costmap_queue)
target_link_libraries(costmap_queue PUBLIC nav2_util_core)

nav2_port_package(nav_2d_utils nav2_dwb_controller/nav_2d_utils)
target_link_libraries(nav_2d_utils PUBLIC nav2_util_core nav2_costmap_2d)

nav2_port_package(dwb_core nav2_dwb_controller/dwb_core)
target_link_libraries(dwb_core PUBLIC nav2_util_core nav2_costmap_2d costmap_queue nav_2d_utils)

nav2_port_package(dwb_plugins nav2_dwb_controller/dwb_plugins)
target_link_libraries(dwb_plugins PUBLIC dwb_core)

nav2_port_package(dwb_critics nav2_dwb_controller/dwb_critics)
target_link_libraries(dwb_critics PUBLIC dwb_core nav2_costmap_2d costmap_queue)

# The graceful controller: a controller whose whole point is that it needs no
# separate smoothing stage, because its control law is smooth by construction.
# Like the rotation shim it is a single plugin that wraps no other, so it is one
# more whole-archive entry on the controller_server line rather than a server of
# its own.
nav2_port_package(nav2_graceful_controller nav2_graceful_controller)
target_link_libraries(nav2_graceful_controller PUBLIC nav2_util_core nav2_costmap_2d)

# ---- MPPI (nav2_mppi_controller) -------------------------------------------
# The controller upstream selects by default, and the one this port was still
# missing. Its shape is DWB's - a sampling controller with a critic set - but
# where DWB scores a hand-written trajectory generator, MPPI samples noise
# around its own control sequence and re-weights by cost.
#
# The Humble-generation `xtensor` dependency is gone in 1.5.1: the optimizer is
# local, and the motion models, critics and trajectory validators are plain
# Eigen, so nothing new has to be vendored. Upstream splits the package into
# four libraries (controller / motion models / critics / trajectory
# validators); they are built as one archive here, because the other three are
# only ever reached through the controller plugin's registration objects and
# nothing else links them. `-fconcepts` is what upstream passes to the critics
# on GCC; the rest of the optimizer is ordinary C++20.
nav2_port_package(nav2_mppi_controller nav2_mppi_controller)
target_link_libraries(nav2_mppi_controller PUBLIC
  nav2_util_core nav2_costmap_2d)
target_compile_options(nav2_mppi_controller PRIVATE -fconcepts)

nav2_port_package(nav2_smoother nav2_smoother)
target_link_libraries(nav2_smoother PUBLIC
  nav2_util_core nav2_costmap_2d nav2_behavior_tree)

nav2_port_package(nav2_behaviors nav2_behaviors)
target_link_libraries(nav2_behaviors PUBLIC
  nav2_util_core nav2_costmap_2d nav2_behavior_tree)

# The two nodes that sit between the controller and the wheels rather than
# between the planner and the controller. Neither hosts a plugin - each is a
# single class - so neither needs a whole-archive entry.
#
# `nav2_velocity_smoother` turns a commanded velocity into one inside the
# platform's acceleration envelope (`max_velocity`, `max_accel`, `max_decel`,
# an optional deadband, and a watchdog that zeroes the output when the input
# stops). It reaches for nothing but nav2_util: `nav2_util::TwistPublisher` and
# `TwistSubscriber` are what carry both ends of its chain.
nav2_port_package(nav2_velocity_smoother nav2_velocity_smoother)
target_link_libraries(nav2_velocity_smoother PUBLIC nav2_util_core)

# `nav2_collision_monitor` stops or slows a velocity command when the sensors
# disagree with it. Its observation sources build their own subscribers
# (`Scan`, `PointCloud`, `Range`, `CostmapSource`, `PolygonSource`) and its
# `CostmapSource` reads a `nav2_costmap_2d::Costmap2D` straight out of the
# costmap topic, which is why the costmap library is on its link line.
#
# `PointCloud` is the one source that reaches for a `point_cloud_transport`;
# see include/point_cloud_transport/point_cloud_transport.hpp for what that
# amounts to here (nothing at rclcpp 28, which is what the shim reports).
nav2_port_package(nav2_collision_monitor nav2_collision_monitor)
target_link_libraries(nav2_collision_monitor PUBLIC
  nav2_util_core nav2_costmap_2d)

# `photo_at_waypoint` is the one task executor here that needs OpenCV, which
# this toolchain does not ship; `wait_at_waypoint` and the follower itself are
# unaffected.
nav2_port_package(nav2_waypoint_follower nav2_waypoint_follower
  plugins/photo_at_waypoint.cpp)
# nav2_util_core only. The waypoint follower sends NavigateToPose goals and
# reads poses out of a tf buffer; it builds no behavior tree and touches no
# costmap, and upstream's own CMakeLists depends on neither.
target_link_libraries(nav2_waypoint_follower PUBLIC nav2_util_core)

nav2_port_package(nav2_lifecycle_manager nav2_lifecycle_manager)
target_link_libraries(nav2_lifecycle_manager PUBLIC nav2_util_core)

nav2_port_package(nav2_bt_navigator nav2_bt_navigator)
target_link_libraries(nav2_bt_navigator PUBLIC
  nav2_util_core nav2_behavior_tree nav2_costmap_2d)

# ---- docking and route -----------------------------------------------------
# The two nav2 subsystems that are not on the costmap-and-controller path at
# all, ported for their compile-and-load half.
#
# `opennav_docking_core` is header-only - it is the `ChargingDock` /
# `NonChargingDock` plugin interfaces and nothing else - so it gets no library
# and no `target_link_libraries` line; its include directory is already in
# NAV2_INCLUDE_DIRS through the `*/*/include` glob.
#
# `nav2_route` needs two headers this toolchain does not ship as packages and
# does have as vendored sources: `nanoflann.hpp` (the spatial tree behind
# `NodeSpatialTree`) and `nlohmann/json.hpp` (the GeoJSON graph reader and
# writer). Both are in third_party/include, which is NAV2_TP, so they are on
# every one of these targets' include paths already.
nav2_port_package(opennav_docking nav2_docking/opennav_docking)
target_link_libraries(opennav_docking PUBLIC
  nav2_util_core nav2_costmap_2d nav2_graceful_controller yaml-cpp)

nav2_port_package(opennav_docking_bt nav2_docking/opennav_docking_bt)
target_link_libraries(opennav_docking_bt PUBLIC nav2_behavior_tree opennav_docking)

nav2_port_package(nav2_route nav2_route)
target_link_libraries(nav2_route PUBLIC
  nav2_util_core nav2_costmap_2d nav2_behavior_tree)

# ---- the nav2 servers, and the manager that brings them up -----------------
# Upstream's `planner_server`, `controller_server`, `bt_navigator` and
# `lifecycle_manager` are the four binaries that turn these libraries into a
# running stack: the servers own the plugins and the manager drives them from
# unconfigured to active over `/<node>/change_state`.
#
# Plugin registration in the shim is a static registry
# (pluginlib/class_loader.hpp), so an object file that only registers a plugin
# in a static initialiser - and is never named by a symbol - is dropped by a
# plain archive link; the lookup then fails with "no registered plugin matching
# ...". Whole-archive keeps those objects, one library at a time, so each server
# only carries the plugins it actually loads.
function(nav2_link_plugin_libraries target)
  set(_archives "")
  # Named as link libraries as well as whole-archived below. The option below
  # is a bare path: it puts the archive on the link line but on nothing else -
  # it creates no build dependency, and it carries none of the archive's own
  # libraries with it, so a plugin whose PRIVATE dependency is not named by some
  # other `target_link_libraries` fails to link ("cannot find .../libfoo.a", or
  # undefined references into Smac's OMPL). Naming them here puts them on the
  # dependency graph and lets their dependencies follow them.
  target_link_libraries(${target} PRIVATE ${ARGN})
  foreach(library ${ARGN})
    list(APPEND _archives "$<TARGET_FILE:${library}>")
  endforeach()
  # All of them in one group, not a group each: CMake drops repeated
  # occurrences of the same link option, so writing the pair once per library
  # collapses to `--whole-archive a --no-whole-archive b c` - only the first
  # archive is actually whole-archived and every plugin in the others is
  # silently dropped, which shows up much later as "no registered plugin
  # matching ...".
  if(MSVC)
    foreach(archive ${_archives})
      target_link_options(${target} PRIVATE "/WHOLEARCHIVE:${archive}")
    endforeach()
  else()
    target_link_options(${target} PRIVATE
      "-Wl,--whole-archive" ${_archives} "-Wl,--no-whole-archive")
  endif()
endfunction()

function(nav2_server target source node_library)
  add_executable(${target} apps/${source})
  target_compile_options(${target} PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
  target_link_libraries(${target} PRIVATE ${node_library} ros2_shim_params)
  nav2_link_plugin_libraries(${target} ${ARGN})
endfunction()

# The global costmap's layers, the planner plugin and - on activation - the
# ComputePathToPose action, all from the parameters file.
nav2_server(planner_server planner_server_node.cpp nav2_planner
  nav2_costmap_2d nav2_navfn_planner nav2_smac_planner)
# The local costmap's layers, the progress/goal checkers that live in
# nav2_controller itself, and a controller plugin. nav2_controller is in the
# whole-archive list as well as being the node library: its plugins/ objects
# exist only to register themselves, so a plain archive link would drop every
# one of them and `SimpleProgressChecker` would come back "not registered".
nav2_server(controller_server controller_server_node.cpp nav2_controller
  nav2_controller nav2_costmap_2d nav2_regulated_pure_pursuit_controller
  nav2_rotation_shim_controller dwb_core dwb_plugins dwb_critics
  nav2_graceful_controller nav2_mppi_controller)
# The navigator plugins, plus every BT node the behavior trees are built from:
# the BT nodes register through nav2_shim::bt_registry::registerNodes() rather
# than pluginlib, but they are still a whole-archive entry - the objects that
# implement the generated wrappers and the registry are only referenced from
# inside the archive itself.
# btcpp_groot_stub is in the list because it has to sit *before* the BT.CPP
# archive on the link line: the stub's own references to StatusChangeLogger are
# resolved by that archive, and GNU ld only looks forward.
#
# nav2_bt_navigator is both the node library and a whole-archive entry, like
# nav2_controller above: `NavigateToPoseNavigator` and
# `NavigateThroughPosesNavigator` reach the process only through pluginlib, so
# the objects that register them carry no symbol anyone references. Linked as a
# plain archive they are dropped, and the cost is paid at configure time -
# "Failed to create navigator id navigate_to_pose. Exception: pluginlib: no
# registered plugin matching 'nav2_bt_navigator::NavigateToPoseNavigator'".
nav2_server(bt_navigator bt_navigator_node.cpp nav2_bt_navigator
  nav2_bt_navigator nav2_behavior_tree nav2_costmap_2d btcpp_groot_stub)
# The path smoother, and the waypoint navigator that drives bt_navigator once per
# pose. Both are plugin-hosting servers of the same kind as nav2_controller
# above, so both name their own library in the whole-archive list as well as as
# the node library: `nav2_smoother::SimpleSmoother` and
# `nav2_waypoint_follower::WaitAtWaypoint` reach the process only through the
# plugin name in a parameters file, and a plain archive link drops their
# registering objects ("no registered plugin matching ..." at configure time).
nav2_server(smoother_server smoother_server_node.cpp nav2_smoother
  nav2_smoother nav2_costmap_2d)
# No nav2_behavior_tree and no nav2_costmap_2d here: upstream's package depends
# on neither (its CMakeLists lists robot_localization and nav2_util), and
# pulling nav2_behavior_tree in would drag BT.CPP onto the link line for a node
# that never builds a tree - which then needs btcpp_groot_stub placed ahead of
# the archive for no benefit.
nav2_server(waypoint_follower waypoint_follower_node.cpp nav2_waypoint_follower
  nav2_waypoint_follower)

# The recovery branch's action servers: `spin`, `backup`, `drive_on_heading` and
# `wait`, all of them plugins that nav2_behaviors registers from its own
# objects. Like nav2_controller above, the library is in the whole-archive list
# as well as being the node library - the only thing that names
# `nav2_behaviors::Spin` and friends is the plugin name in a parameters file, so
# a plain archive link drops every one of them and the server comes up with "no
# registered plugin matching 'nav2_behaviors::Spin'" at configure time.
# nav2_costmap_2d is here for the collision checkers the server builds for those
# plugins (`CostmapSubscriber`, `CostmapTopicCollisionChecker`).
nav2_server(behavior_server behavior_server_node.cpp nav2_behaviors
  nav2_behaviors nav2_costmap_2d)

# The acceleration limiter and the safety monitor. Both are single-class nodes
# with no pluginlib names in their parameter blocks, so unlike the servers above
# they need no whole-archive entries; they are in the stack for what they do to
# the velocity stream, not for what they load.
nav2_server(velocity_smoother velocity_smoother_node.cpp nav2_velocity_smoother)
nav2_server(collision_monitor collision_monitor_node.cpp nav2_collision_monitor)

# The manager itself holds no plugins - it only talks to the others' services.
nav2_server(lifecycle_manager lifecycle_manager_node.cpp nav2_lifecycle_manager)

# The two subsystems off the costmap path. Neither is in nav2_stack.bat's
# `node_names`, so neither is brought up by the manager that drives the seven -
# they are here to be compiled, linked and loaded, which is what a plugin
# library that nothing references needs a whole-archive entry for. Both name
# their own library in the whole-archive list for the same reason the other
# plugin hosts do: every dock plugin and every edge scorer reaches the process
# through a plugin name in a parameters file.
nav2_server(docking_server docking_server_node.cpp opennav_docking
  opennav_docking opennav_docking_bt nav2_graceful_controller)
nav2_server(route_server route_server_node.cpp nav2_route
  nav2_route nav2_costmap_2d)

# Diagnostic (not a ctest): loads a behavior tree the way bt_navigator does, with
# a breadcrumb per step, to pin down where that path crashes.
add_executable(bt_smoke apps/bt_smoke.cpp)
target_compile_options(bt_smoke PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(bt_smoke PRIVATE nav2_bt_navigator nav2_behavior_tree ros2_shim_params)
nav2_link_plugin_libraries(bt_smoke nav2_bt_navigator nav2_behavior_tree nav2_costmap_2d btcpp_groot_stub)

# The other end of the action interface: sends one NavigateToPose goal at the
# running stack and reports what the behavior tree did with it. It speaks only
# the action protocol and reads /odom, /plan and /cmd_vel to prove the servers
# did something, so it needs no plugins - just the shim itself.
add_executable(nav2_goal apps/nav2_goal.cpp)
target_compile_options(nav2_goal PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(nav2_goal PRIVATE ros2_shim)

# The other end of the action surface: ComputePathToPose, SmoothPath and
# FollowWaypoints, chained. `nav2_goal` covers the behavior tree and the two
# servers it drives; this covers the three servers no tree in this port calls -
# planner_server's action is reachable both ways, but smoother_server's and
# waypoint_follower's are only reachable through a caller, and this is it. Like
# nav2_goal it speaks only the action protocol, so it needs no plugins.
add_executable(nav2_waypoints apps/nav2_waypoints.cpp)
target_compile_options(nav2_waypoints PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(nav2_waypoints PRIVATE ros2_shim)

# The state machine those servers run on, driven the way the manager drives it:
# `/<node>/change_state` plus the two query services.
add_executable(test_lifecycle_services tests/lifecycle_services.cpp)
target_compile_options(test_lifecycle_services PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_lifecycle_services PRIVATE ros2_shim)

# ---- nav2_amcl -------------------------------------------------------------
# Monte-Carlo localization, ported straight from
# nav2_extracted/navigation2-1.5.1/nav2_amcl. The particle filter, map ray
# casting and laser sensor models are C, so they build as their own target
# (without the C++ prelude that forced-includes <windows.h> et al).
set(NAV2_AMCL ${NAV2_SRC}/nav2_amcl)
set(NAV2_AMCL_CPP
  ${NAV2_AMCL}/src/amcl_node.cpp
  ${NAV2_AMCL}/src/map/map_cspace.cpp
  ${NAV2_AMCL}/src/motion_model/differential_motion_model.cpp
  ${NAV2_AMCL}/src/motion_model/omni_motion_model.cpp
  ${NAV2_AMCL}/src/sensors/laser/laser.cpp
  ${NAV2_AMCL}/src/sensors/laser/beam_model.cpp
  ${NAV2_AMCL}/src/sensors/laser/likelihood_field_model.cpp
  ${NAV2_AMCL}/src/sensors/laser/likelihood_field_model_prob.cpp)
# pf_draw.c / map_draw.c need the Robot ToolKit (rtk.h); they only draw debug
# windows and nothing else references them.
set(NAV2_AMCL_C
  ${NAV2_AMCL}/src/pf/pf.c
  ${NAV2_AMCL}/src/pf/pf_kdtree.c
  ${NAV2_AMCL}/src/pf/pf_pdf.c
  ${NAV2_AMCL}/src/pf/pf_vector.c
  ${NAV2_AMCL}/src/pf/eig3.c
  ${NAV2_AMCL}/src/map/map.c
  ${NAV2_AMCL}/src/map/map_range.c)

add_library(nav2_amcl_core_c STATIC ${NAV2_AMCL_C})
target_include_directories(nav2_amcl_core_c PUBLIC
  ${NAV2_AMCL}/include ${NAV2_INCLUDE_DIRS} ${NAV2_TP})
# pf.c, pf_pdf.c and amcl_node.cpp all include nav2_amcl/portable_utils.hpp,
# which defines a `static double drand48()` fallback under `#ifndef
# HAVE_DRAND48`. Upstream's own nav2_amcl/CMakeLists.txt answers that macro
# with a configure check (`check_symbol_exists(drand48 stdlib.h ...)`) and two
# `target_compile_definitions`, which the shim does not run because it compiles
# the sources itself -- so the fallback was compiled on every platform. On
# MinGW that is the definition amcl needs; on glibc it collides with the one in
# <stdlib.h> ("static declaration of 'drand48' follows non-static
# declaration"). Ask the same question upstream asks, so each platform gets
# the definition it should. PUBLIC because the C++ amcl_node.cpp needs the
# answer too.
include(CheckSymbolExists)
check_symbol_exists(drand48 "stdlib.h" NAV2_SHIM_HAVE_DRAND48)
if(NAV2_SHIM_HAVE_DRAND48)
  target_compile_definitions(nav2_amcl_core_c PUBLIC HAVE_DRAND48)
endif()

add_library(nav2_amcl STATIC ${NAV2_AMCL_CPP})
target_include_directories(nav2_amcl PUBLIC
  ${NAV2_AMCL}/include ${NAV2_INCLUDE_DIRS} ${NAV2_TP})
target_link_libraries(nav2_amcl PUBLIC ros2_shim nav2_util_core nav2_amcl_core_c)
target_compile_options(nav2_amcl PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)

# AMCL picks its motion model by name at run time (differential / omni, see
# `robot_model_type`), and the objects that register those plugins are pulled
# into the archive without anything referencing their symbols - a plain archive
# link drops them and the lookup then fails with "no registered plugin matching
# nav2_amcl::DifferentialMotionModel". Whole-archive keeps the registrations.
function(amcl_link_plugin_registry target)
  if(MSVC)
    target_link_options(${target} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:nav2_amcl>")
  else()
    target_link_options(${target} PRIVATE
      "-Wl,--whole-archive" "$<TARGET_FILE:nav2_amcl>" "-Wl,--no-whole-archive")
  endif()
endfunction()

add_executable(amcl ${NAV2_AMCL}/src/main.cpp)
target_include_directories(amcl PRIVATE ${NAV2_AMCL}/include ${NAV2_INCLUDE_DIRS})
target_link_libraries(amcl PRIVATE nav2_amcl ros2_shim_params)
target_compile_options(amcl PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
amcl_link_plugin_registry(amcl)

# Runs AmclNode against the shared-memory graph, making the configure/activate
# transitions itself - AMCL builds its subscriptions and services in
# on_configure(), and nothing here drives the lifecycle over services. See
# include/ros2_shim/lifecycle_runner.hpp.
add_executable(amcl_bridge_node apps/amcl_bridge_node.cpp)
target_include_directories(amcl_bridge_node PRIVATE
  ${NAV2_AMCL}/include ${NAV2_INCLUDE_DIRS})
target_link_libraries(amcl_bridge_node PRIVATE nav2_amcl ros2_shim_params)
target_compile_options(amcl_bridge_node PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
amcl_link_plugin_registry(amcl_bridge_node)

# ---- nav2_map_server -------------------------------------------------------
# Upstream's map_io.cpp is the only file in navigation2 that needs an image
# library: it reads and writes map images through Magick++. The port replaces
# that dependency rather than shipping it - include/Magick++.h implements the
# subset map_io.cpp calls: PGM, which is the format a ROS2 map is actually in
# (and the one `map_saver` picks by itself for every mode but `scale`), and
# 8-bit PNG, which is what `scale` needs - it is the only mode that writes
# transparency, which is exactly why `map_saver` defaults to png there. The
# header's own comment has the details.
#
# Neither node loads a plugin, so neither needs the whole-archive treatment: the
# map server has no plugin interface, and the saver's only dependency is the
# writer.
# The vector-object server used to be left out of this library because it wants
# libuuid (`uuid/uuid.h`), which MSYS2 does not ship. It is back in: the shim
# now carries its own <uuid/uuid.h> (include/uuid/uuid.h) with the nine libuuid
# calls those two files make, so all of nav2_map_server compiles here.
nav2_port_package(nav2_map_server nav2_map_server)
# map_io.cpp parses the YAML sidecar itself (yaml-cpp, built at the top of this
# file), uses nav2_util's geometry helpers to build the map origin, and deflates
# its PNG coder with zlib. `z` resolves out of the MSYS2 UCRT64 sysroot the rest
# of this build already uses, and the `-static` link options above pull in
# libz.a, so a map server still needs nothing next to it at run time. PUBLIC
# because the coder is header-only and inline: the call sites land in whoever
# links the library.
target_link_libraries(nav2_map_server PUBLIC nav2_util_core yaml-cpp z)
nav2_server(map_server map_server_node.cpp nav2_map_server)
nav2_server(map_saver map_saver_node.cpp nav2_map_server)
# The two map-server nodes that publish metadata rather than a map. Neither
# loads a plugin, so neither needs the whole-archive treatment - they are here
# because upstream builds them and a stack that is missing them is missing the
# services a costmap filter or an exclusion zone asks for.
nav2_server(costmap_filter_info_server costmap_filter_info_server_node.cpp nav2_map_server)
nav2_server(vector_object_server vector_object_server_node.cpp nav2_map_server)

# The one-shot saver: `map_saver_cli -t /map -f out --fmt pgm` (or `--fmt png`
# for a scale-mode map, which keeps its alpha channel) subscribes
# to the topic, writes one PGM plus its YAML sidecar and exits. Built straight
# from upstream's main, which the package glob above leaves out of the library
# for defining `main` - it configures a MapSaver and calls saveMapTopicToFile()
# itself, so saving a map needs no service call and no second process.
add_executable(map_saver_cli ${NAV2_SRC}/nav2_map_server/src/map_saver/main_cli.cpp)
target_compile_options(map_saver_cli PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(map_saver_cli PRIVATE nav2_map_server ros2_shim_params)

# The reader and the writer, against files another tool could also read.
# The closed-form geometry Smac plans with: every Dubins word has to drive to
# the goal it was solved for, the paths have to be arc-length parameterised and
# no interpolated step may turn tighter than the turning radius.
add_executable(test_ompl_spaces tests/ompl_spaces.cpp)
target_compile_options(test_ompl_spaces PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_ompl_spaces PRIVATE ompl_shim)

# The one rclcpp API whose whole meaning is delivery: which parameter change
# reaches which callback. Smac relies on it to notice a new global-costmap
# resolution, and a handler that never fires is indistinguishable from a correct
# one until the planner is planning against a stale costmap.
add_executable(test_parameter_event_handler tests/parameter_event_handler.cpp)
target_compile_options(test_parameter_event_handler PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_parameter_event_handler PRIVATE ros2_shim)

# pluginlib looks a plugin up by the name <pkg>/<class> in a plugin description
# file, or by the stringified type at the export site. The shim has no XML, so
# the export site is the only source of names - which is why an exported alias
# has to resolve under the alias and not under the type it aliases.
add_executable(test_plugin_names tests/plugin_names.cpp)
target_compile_options(test_plugin_names PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_plugin_names PRIVATE ros2_shim)

# The graph file every process keeps for the others: a rewrite has to land
# while somebody is reading it, or the node that just registered a service
# disappears from the graph other processes answer questions from.
add_executable(test_graph_rewrite tests/graph_rewrite.cpp)
target_compile_options(test_graph_rewrite PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_graph_rewrite PRIVATE ros2_shim)

# The same contract with the reader in another process: the holder cannot be a
# handle this process opened, because the failure mode being tested is "another
# process has the file and the rewrite is refused". See the header of the file.
add_executable(test_graph_rewrite_cross_process tests/graph_rewrite_cross_process.cpp)
target_compile_options(test_graph_rewrite_cross_process PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_graph_rewrite_cross_process PRIVATE ros2_shim)

# A node on simulated time has to read the ramp's `/clock` back from `now()`.
# The wall clock is 2026 and the simulation starts at 0, so a component that
# pairs `now()` with a stamped message is decades away from it -- which is what
# the costmap and the controller do on every update.
add_executable(test_clock tests/clock.cpp)
target_compile_options(test_clock PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_clock PRIVATE ros2_shim)

# The shim's own <uuid/uuid.h>: parse and unparse have to be exact inverses, or
# a shape identifier loses its identity on the way through a parameters file.
add_executable(test_uuid tests/uuid.cpp)
target_compile_options(test_uuid PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_uuid PRIVATE ros2_shim)

# `-r FROM:=TO`. A remap that is silently dropped does not fail anything: the
# two nodes end up on two different topics that both have traffic, and the robot
# never moves. `nav2_stack.bat --safety` is built on this, so the rules - the
# spelling of both sides, the namespace they resolve against, the first-match
# order, and `__node` / `__ns` next to them - are pinned here.
add_executable(test_remap tests/remap.cpp)
target_compile_options(test_remap PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_remap PRIVATE ros2_shim)

# `-p NAME:=VALUE`: what a node ends up with. The list form `[a, b, c]` is the
# one shape a bare word cannot express, and the one whose loss is invisible: an
# override that was dropped reads exactly like an override nobody gave, so the
# node answers with the default it kept. `slam_toolbox` is where that bites -
# its starting pose is `map_start_pose`, a three element double array - so
# `-p map_start_pose:=[0.0,0.0,0.0]` used to leave a localisation node saying
# "Map starting pose not specified" (bridge/docs/03-validation.md, evidence 47).
add_executable(test_param_args tests/param_args.cpp)
target_compile_options(test_param_args PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_param_args PRIVATE ros2_shim ros2_shim_params)

# The six parameter services (`<node>/get_parameters` and friends). They are
# the whole reason `ros2 param` works at all, and they are the one place where a
# caller the node has never met can change what it is doing: what is pinned here
# is the semantics the srv files describe - request order, PARAMETER_NOT_SET for
# a name that is not set, a refusal that names the reason, all-or-nothing for
# the atomic call, and the prefix/depth answer of `list_parameters` - together
# with the two things a nearly-correct implementation gets wrong invisibly: a
# type change that is accepted (and makes the node throw later) and an
# `start_parameter_services(false)` that is ignored (and puts twelve channels on
# every private helper node nav2 builds).
add_executable(test_parameter_service tests/parameter_service.cpp)
target_compile_options(test_parameter_service PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_parameter_service PRIVATE ros2_shim)

add_executable(test_map_io tests/map_io.cpp)
target_compile_options(test_map_io PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_map_io PRIVATE nav2_map_server ros2_shim_params)

# `/parameter_events`: the one topic a node publishes without being asked, and
# the only way a process that does not know a node's service names watches its
# parameters change. It is also the one place a shim node writes into a channel
# several writers share, which the ring's own mutex has to survive. The
# negative half matters as much as the payload: upstream nav2 turns the
# publisher off on every internal helper node
# (`start_parameter_event_publisher(false)`), so a node that ignores the option
# puts another channel on every node a server builds - and that is invisible
# from the outside until the graph is full of them.
add_executable(test_parameter_events tests/parameter_events.cpp)
target_compile_options(test_parameter_events PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_parameter_events PRIVATE ros2_shim)

# `/rosout`: the topic every node's log lands on, and the last topic a real
# robot's `ros2 topic list` had that the shim did not. The `RCLCPP_*` macros the
# whole ported tree logs through only reached stderr, so nothing but a console
# could see what nav2 was saying. What this pins is the message - its text, the
# severity *number* the message type carries, the node's name and the call site
# - plus the two things a nearly-correct version gets wrong invisibly: a node
# that declined the topic (`enable_rosout(false)`) must be silent on it rather
# than borrowing another node's publisher, and a node that dies must take its
# publisher out of the graph with it.
add_executable(test_rosout tests/rosout.cpp)
target_compile_options(test_rosout PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_rosout PRIVATE ros2_shim)

# `<action>/_action/status`: the seventh channel of an action, the one an
# application subscribes to instead of blocking on `async_get_result`.
# `nav2_waypoint_cycle` is why it exists and what it is shaped for.
add_executable(test_action_status tests/action_status.cpp)
target_compile_options(test_action_status PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_action_status PRIVATE ros2_shim)

# Live diagnostic (not a ctest): prints `/parameter_events` as another process
# sees it, which is the one thing the test above cannot show - the reader there
# is in the writer's process. Run it next to `ros2 param set`.
add_executable(parameter_events_probe tests/parameter_events_probe.cpp)
target_compile_options(parameter_events_probe PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(parameter_events_probe PRIVATE ros2_shim)

# The writing half of that diagnostic: a node that logs, for
# `ros2 topic echo /rosout` in another window to read back. The in-process test
# shows that the message is right; this is the one that shows a *different*
# program can see it, which is the whole point of putting it on a topic.
add_executable(rosout_probe tests/rosout_probe.cpp)
target_compile_options(rosout_probe PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(rosout_probe PRIVATE ros2_shim)

# A service and a client are both readers of a channel a *previous* instance of
# the same name filled. The ring keeps those messages, and this reader has to
# skip them: its cursor starts at 0, which is the oldest slot still in the ring,
# so a server that comes up under a name it has used before would answer a
# request nobody is waiting for, and a client would hand back a response to a
# call it never made. Both constructors call `seek_to_latest()` for exactly
# that, and both are pinned here - including the restart loop, which is how this
# shows up in practice (relaunching `ros2 run` reuses the channel name).
add_executable(test_channel_ring_lifecycle tests/channel_ring_lifecycle.cpp)
target_compile_options(test_channel_ring_lifecycle PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/include/ros2_shim/prelude.hpp)
target_link_libraries(test_channel_ring_lifecycle PRIVATE ros2_shim)

# Throughput and latency of the *whole* publish path: struct -> serialize ->
# shared-memory slot -> deserialize -> struct. `shm_transport`'s own benchmark
# measures the channel alone (64 byte blobs, no codec); this is what a nav2
# node actually pays, for the three payload sizes a stack really uses. It is a
# benchmark, so it is **not** registered as a ctest - run it by hand:
#
#     nav2/ros2_shim/build/bench_real_path
#
# No prelude injection on purpose: this file uses the transport, the codec and
# the generated message structs directly, never the rclcpp shim, so the
# prelude would only add surface for the benchmark to accidentally depend on.
add_executable(bench_real_path tests/bench_real_path.cpp)
target_link_libraries(bench_real_path PRIVATE ros2_shim)

if(MSVC)
  set(SHIM_WARN /W4)
else()
  set(SHIM_WARN -Wall -Wextra)
endif()
target_compile_options(test_messages PRIVATE ${SHIM_WARN})
target_compile_options(compile_check_messages PRIVATE ${SHIM_WARN})
target_compile_options(py_interop PRIVATE ${SHIM_WARN})
target_compile_options(test_shim_node PRIVATE ${SHIM_WARN})
target_compile_options(test_shim_compile PRIVATE ${SHIM_WARN})

enable_testing()
add_test(NAME ros2_shim_messages COMMAND test_messages)
add_test(NAME ros2_shim_compile_check COMMAND compile_check_messages)
# Cross-language interop, as a test and not a habit: all four pairings of the
# C++ harness with its Python mirror (python/py_interop.py), plus a byte-level
# comparison of the two dumps. It was written years of commits ago and never
# registered, so the one contract the whole shim rests on -- that both sides
# speak the same CDR -- could have rotted in silence. The mirror needs no
# toolchain of its own: ctypes for the ring, the generated structs for the
# encoding. A host without Python gets a loud status line instead of a
# configure failure, because the rest of the tree does not depend on it.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME ros2_shim_py_interop COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/interop_matrix.py
    --cxx $<TARGET_FILE:py_interop>
    --tmp ${CMAKE_CURRENT_BINARY_DIR}/interop)
else()
  message(STATUS "ros2_shim: no Python3 interpreter, ros2_shim_py_interop not registered")
endif()
add_test(NAME ros2_shim_node COMMAND test_shim_node)
add_test(NAME ros2_shim_compile COMMAND test_shim_compile)
add_test(NAME nav2_navfn_planning COMMAND test_navfn_planning)
# nav2_ros_common's LifecycleNode and StandardTopicQoS composition check.
# Built since the first nav2 port and never registered either.
add_test(NAME ros2_shim_nav2_ros_common COMMAND test_nav2_ros_common)
add_test(NAME ros2_shim_params_file COMMAND test_params_file)
add_test(NAME ros2_shim_lifecycle_services COMMAND test_lifecycle_services)
add_test(NAME ros2_shim_channel_ring_lifecycle COMMAND test_channel_ring_lifecycle)
add_test(NAME ros2_shim_tf_time COMMAND test_tf_time)
add_test(NAME ros2_shim_tf_convert COMMAND test_tf_convert)
add_test(NAME ros2_shim_tf_buffer_lookup COMMAND test_tf_buffer_lookup)
# Full in-process pipeline: NavFn planning -> Path over shared memory ->
# NavigateToPose action round trip (this is the test that guards the action
# client's buffering of a result that arrives before async_get_result()).
add_test(NAME nav2_e2e_pipeline COMMAND test_e2e)
set_tests_properties(nav2_e2e_pipeline PROPERTIES ENVIRONMENT "NAV2_SHM_LOG_LEVEL=info")
# A trinary map has to survive save -> PGM + YAML -> load cell for cell, and
# the file has to be readable by something other than this program.
add_test(NAME nav2_map_server_map_io COMMAND test_map_io)
add_test(NAME nav2_smac_ompl_spaces COMMAND test_ompl_spaces)
add_test(NAME ros2_shim_parameter_event_handler COMMAND test_parameter_event_handler)
add_test(NAME ros2_shim_plugin_names COMMAND test_plugin_names)
# A rewrite that loses a race with a reader used to be dropped in silence, which
# is how a service that is being served could read as "not available".
add_test(NAME ros2_shim_graph_rewrite COMMAND test_graph_rewrite)
add_test(NAME ros2_shim_graph_rewrite_cross_process COMMAND test_graph_rewrite_cross_process)
add_test(NAME ros2_shim_clock COMMAND test_clock)
add_test(NAME ros2_shim_uuid COMMAND test_uuid)
add_test(NAME ros2_shim_remap COMMAND test_remap)
add_test(NAME ros2_shim_param_args COMMAND test_param_args)
add_test(NAME ros2_shim_parameter_service COMMAND test_parameter_service)
add_test(NAME ros2_shim_parameter_events COMMAND test_parameter_events)
add_test(NAME ros2_shim_rosout COMMAND test_rosout)
add_test(NAME ros2_shim_action_status COMMAND test_action_status)
add_test(NAME slam_toolbox_eigen_pose_graph_solver COMMAND test_eigen_pose_graph_solver)
# Full mapping run: synthetic robot + laser -> slam_toolbox -> /map.
add_test(NAME slam_toolbox_mapping COMMAND test_slam_mapping)
# Karto's laser model: points of a scan have to land on the walls around it.
add_test(NAME slam_toolbox_laser_points COMMAND test_laser_points)
# The pose-graph archive: serialize_map writes it, deserialize_map reads it back
# into a fresh node that has to publish the same grid.
add_test(NAME slam_toolbox_serialization COMMAND test_slam_serialization)
set_tests_properties(slam_toolbox_mapping PROPERTIES ENVIRONMENT "NAV2_SHM_LOG_LEVEL=info")

# Every test here talks over the same shared-memory channels the bridge daemon
# mirrors the robot into -- `/scan`, `/odom`, `/tf`, `/clock`, `/map` -- and a
# channel has a single writer by contract. Two of these tests publish exactly
# those topics, so with the bridge up they shared the channel with the mirrored
# robot instead of owning it: `slam_toolbox_mapping` drives an 8 x 6 m room and
# asserts that bounding box, and with the daemon running it measured 12.20 x
# 11.10 m -- the room plus the world outside it. A test whose result depends on
# whether another program happens to be running is not a test, so each one gets
# a namespace of its own (`ros2shim<namespace><topic>`, see 03-validation.md,
# evidence 9). ENVIRONMENT_MODIFICATION appends rather than replaces, so the
# log level set above survives.
get_property(SHIM_TEST_NAMES DIRECTORY PROPERTY TESTS)
foreach(_test IN LISTS SHIM_TEST_NAMES)
  set_tests_properties(${_test} PROPERTIES
    ENVIRONMENT_MODIFICATION "NAV2_SHM_NAMESPACE=set:ctest_${_test}"
    # Same channels as above, so the same hang is possible here: see the note on
    # the bound in `nav2/shm_transport/CMakeLists.txt`. The whole tree is 52 s
    # over 32 tests, and the slowest one is a handful of seconds.
    TIMEOUT 300)
endforeach()
