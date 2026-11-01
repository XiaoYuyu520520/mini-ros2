@echo off
rem ---------------------------------------------------------------------------
rem Environment for the Windows ROS2 shim + gRPC bridge.
rem
rem This file is meant to be CALLed by another script (or from a cmd prompt):
rem
rem     call bridge\windows\bridge_env.bat
rem
rem It does NOT open its own `setlocal`, so every variable it sets stays in the
rem caller's scope - that is what makes the environment "temporary": run
rem `endlocal` / close the window and Windows is back to its previous state.
rem
rem Override anything by setting it beforehand, e.g.
rem     set ROS2_BRIDGE_AGENT=192.168.1.113:50051
rem     call bridge\windows\bridge_env.bat
rem ---------------------------------------------------------------------------

rem Remember which tree this environment came from, before overwriting the
rem marker with this file's own directory.
set "ROS2_BRIDGE_PREVIOUS_WINDOWS=%ROS2_BRIDGE_WINDOWS%"
set "ROS2_BRIDGE_WINDOWS=%~dp0"
for %%I in ("%~dp0..\..") do set "ROS2_BRIDGE_CPU_ROOT=%%~fI"

rem A window that loaded another copy of this package first still carries that
rem copy's derived paths: the values below are only filled in when they are
rem empty, so a second tree inherits the first one's build directory, its
rem NAV2_SHIM_SRC and its log paths. When the directory changes, drop exactly
rem those derived values and let this file compute them again - the ones a
rem person set on purpose (ports, slot sizes, the agent address) are left
rem alone. The comparison uses ROS2_BRIDGE_WINDOWS rather than a dedicated
rem marker because that variable is part of the environment the import keeps:
rem the marker has to survive the switch to be able to detect it.
if /I not "%ROS2_BRIDGE_PREVIOUS_WINDOWS%"=="%ROS2_BRIDGE_WINDOWS%" (
  set "ROS2_BRIDGE_BUILD="
  set "ROS2_BRIDGE_READY_FILE="
  set "ROS2_BRIDGE_LOG="
  set "ROS2_BRIDGE_ERR="
  set "ROS2_BRIDGE_PATH_READY="
  set "NAV2_SHIM_SRC="
  set "NAV2_SHIM_BT_DIR="
  set "ROS2_GRPC_SDK_CONFIG_DIR="
)

rem Make `ros2`, `rviz2` and the demo .bat files reachable by name from this
rem window. The marker keeps a second `call bridge_env.bat` from growing PATH a
rem second time - and it holds the directory the marker was set for, so a
rem second tree puts its own directory in front.
if /I not "%ROS2_BRIDGE_PATH_READY%"=="%ROS2_BRIDGE_WINDOWS%" (
  set "PATH=%ROS2_BRIDGE_WINDOWS%;%PATH%"
  set "ROS2_BRIDGE_PATH_READY=%ROS2_BRIDGE_WINDOWS%"
)

if not defined ROS2_BRIDGE_BUILD set "ROS2_BRIDGE_BUILD=%ROS2_BRIDGE_WINDOWS%build"

rem The daemon's MinGW and gRPC/protobuf DLLs are copied next to it by the
rem build (bridge/windows/cmake/bundle_runtime.cmake), so this window runs on a
rem machine that has no MSYS2 installed. The MSYS2 bin directory is added to
rem PATH only when it happens to exist, as a fallback for a build directory
rem produced without that bundling step - set ROS2_BRIDGE_NO_MSYS2_PATH=1 to
rem leave PATH alone even then.
rem CPU_MSYS2_ROOT is the name tools/_toolchain.py resolves through, so honour
rem it here too; the literal below is only this desk's default.
if not defined ROS2_BRIDGE_MSYS2 if defined CPU_MSYS2_ROOT set "ROS2_BRIDGE_MSYS2=%CPU_MSYS2_ROOT%"
if not defined ROS2_BRIDGE_MSYS2 set "ROS2_BRIDGE_MSYS2=D:\WindowsServer2025_1\software\MSYS2"
set "ROS2_BRIDGE_UCRT64=%ROS2_BRIDGE_MSYS2%\ucrt64\bin"
if not defined ROS2_BRIDGE_NO_MSYS2_PATH if exist "%ROS2_BRIDGE_UCRT64%" set "PATH=%ROS2_BRIDGE_UCRT64%;%PATH%"

rem Shared-memory geometry. Publisher and subscriber must agree, so exporting
rem the same values for every process in this window is what makes the shim
rem channels line up.
rem
rem These are the *sizes* the connector needs, not the defaults in
rem nav2/ros2_shim/include/rclcpp/shm_config.hpp. That file is sized for a
rem nav2 stack alone; once the relay merges a real ROS2 graph into the same
rem bridge, the daemon mirrors ~80 topics at once and the geometry is
rem multiplied by all of them:
rem
rem   2 MB / 8 MB  ->  80 topics  ->  ~2.4 GB of committed shared memory
rem   512 KB / 2 MB -> 80 topics  ->  ~600 MB
rem
rem The largest message measured on the reference stack is a global costmap at
rem 528 KB, so a 2 MB large slot still has ~4x headroom. A channel that is
rem already open keeps the layout it was created with, so the daemon and every
rem nav2 node have to be restarted together after a change.
if not defined NAV2_SHM_SLOT_COUNT set "NAV2_SHM_SLOT_COUNT=8"
if not defined NAV2_SHM_SLOT_SIZE set "NAV2_SHM_SLOT_SIZE=524288"
if not defined NAV2_SHM_LARGE_SLOT_SIZE set "NAV2_SHM_LARGE_SLOT_SIZE=2097152"

rem Where the ported nav2 packages are. The ament shim resolves
rem `$(find-pkg-share <pkg>)` - the token nav2's own params file puts in
rem `bt_search_directories` - by looking in NAV2_SHIM_PREFIX\share\<pkg> first
rem and NAV2_SHIM_SRC\<pkg> second (see
rem nav2/ros2_shim/include/ament_index_cpp/get_package_share_path.hpp). The port
rem builds the extracted sources in place rather than installing a share tree,
rem so NAV2_SHIM_SRC is the one that answers: nav2_bt_navigator's behavior_trees
rem are at <NAV2_SHIM_SRC>\nav2_bt_navigator\behavior_trees.
if not defined NAV2_SHIM_SRC set "NAV2_SHIM_SRC=%ROS2_BRIDGE_CPU_ROOT%\nav2\nav2_extracted\navigation2-1.5.1"

rem Behavior trees this port loads instead of upstream's, when upstream's need a
rem node the port has no action server for. nav2_shim_overrides.yaml points
rem `default_nav_to_pose_bt_xml` here with a `$(env NAV2_SHIM_BT_DIR)` token, so
rem the file can move without editing YAML.
if not defined NAV2_SHIM_BT_DIR set "NAV2_SHIM_BT_DIR=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\bt"

rem The bridge daemon mirrors remote topics into `%TEMP%\nav2_shim_graph`, so every
rem process in this window sees the same graph regardless of who started it
rem first. NAV2_SHM_GRAPH_DIR is the name the shim actually reads (see
rem nav2/ros2_shim/include/ros2_shim/graph.hpp); ROS2_SHM_GRAPH_DIR is kept as an
rem alias for older scripts.
if not defined NAV2_SHM_GRAPH_DIR set "NAV2_SHM_GRAPH_DIR=%TEMP%\nav2_shim_graph"
if not defined ROS2_SHM_GRAPH_DIR set "ROS2_SHM_GRAPH_DIR=%NAV2_SHM_GRAPH_DIR%"

rem Log level of the shim's rclcpp-compatible logger: debug | info | warn |
rem error | fatal. `info` hides the DEBUG chatter of the ported nav2 code;
rem set NAV2_SHM_LOG_LEVEL=debug before calling this file to get it back.
if not defined NAV2_SHM_LOG_LEVEL set "NAV2_SHM_LOG_LEVEL=info"

rem Discovery / connection defaults for the gRPC SDK client.
if not defined ROS2_GRPC_SDK_CONFIG_DIR set "ROS2_GRPC_SDK_CONFIG_DIR=%ROS2_BRIDGE_CPU_ROOT%\gRPC\ros2_gRPC_sdk\config"

rem `ros2.bat` and the demo scripts start the daemon on demand unless this is
rem set to 0.
if not defined ROS2_BRIDGE_AUTOSTART set "ROS2_BRIDGE_AUTOSTART=1"

rem Waiting for the daemon: it creates ROS2_BRIDGE_READY_FILE as soon as its
rem first connection attempt has settled, and wait_bridge_ready.bat polls for
rem that file. A cold start needs the LAN scan plus the first topic sync, so
rem the budget is in whole seconds rather than a guess in milliseconds; 0
rem turns the wait off.
if not defined ROS2_BRIDGE_READY_FILE set "ROS2_BRIDGE_READY_FILE=%ROS2_BRIDGE_BUILD%\ros2_bridge_daemon.ready"
if not defined ROS2_BRIDGE_WAIT_MS  set "ROS2_BRIDGE_WAIT_MS=30000"

if not defined ROS2_BRIDGE_LOG      set "ROS2_BRIDGE_LOG=%ROS2_BRIDGE_BUILD%\ros2_bridge_daemon.log"
if not defined ROS2_BRIDGE_ERR      set "ROS2_BRIDGE_ERR=%ROS2_BRIDGE_BUILD%\ros2_bridge_daemon.err.log"
rem A detached daemon appends to those two files for its whole life, so past
rem this many bytes the daemon renames a log to <path>.1 and starts a fresh one
rem (bridge/windows/src/bridge_daemon.cpp). 0 turns the check off.
if not defined ROS2_BRIDGE_LOG_MAX  set "ROS2_BRIDGE_LOG_MAX=16777216"
