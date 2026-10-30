@echo off
rem ---------------------------------------------------------------------------
rem slam_demo.bat - run slam_toolbox on the Windows side of the bridge.
rem
rem     slam_demo.bat                                   /scan -> /map
rem     slam_demo.bat -p map_name:=/slam_map --duration 60
rem     slam_demo.bat --mode async
rem
rem The cross-platform twin of this script is bridge\scripts\run_slam_demo.py:
rem the same options, on Windows and on Linux. Change one, change the other.
rem
rem It calls bridge_env.bat first, so PATH, the shared-memory geometry and the
rem gRPC SDK config live in this cmd session only - close the window and Windows
rem is back to normal.
rem
rem What it needs in shared memory is /scan and /tf, which the bridge daemon
rem mirrors in from the Ubuntu robot:
rem
rem     ros2_bridge.bat start
rem
rem The node publishes its map on /map by default, and that is the same topic
rem the daemon mirrors the Ubuntu map onto. When SLAM is meant to be the map
rem source, restart the daemon without /map first:
rem
rem     ros2_bridge.bat stop
rem     ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan ^
rem         --mirror /tf --mirror /tf_static
rem
rem Upstream's own *_node binaries only spin - a lifecycle manager drives
rem configure/activate in a normal ROS2 deployment and there is none on this
rem side of the bridge. slam_bridge_node does both transitions itself.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
call "%~dp0bridge_env.bat"

set "SLAM=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\build\slam_toolbox_port\slam_bridge_node.exe"
if defined NAV2_SLAM_EXE set "SLAM=%NAV2_SLAM_EXE%"

if not exist "%SLAM%" (
  echo [slam_demo] %SLAM% not found 1>&2
  echo [slam_demo] build it with:  cmake --build nav2\ros2_shim\build 1>&2
  exit /b 1
)

rem slam_toolbox defaults to the `base_footprint` robot frame; the bundled sim
rem publishes `base_link`. Extra parameters go through NAV2_SLAM_PARAMS.
rem
rem Only the frame is preset, and it steps aside as soon as the caller says
rem anything about the robot frame themselves - `base_frame` anywhere in the
rem arguments, or a `--params-file` that brings its own. Other parameters
rem (`map_name`, `map_update_interval`, ...) do not get in the way of it: they
rem are the common case, and dropping the preset for them used to leave the
rem node looking for a `base_footprint` frame that this simulator never
rem publishes. An explicit NAV2_SLAM_PARAMS always applies.
if not defined NAV2_SLAM_PARAMS (
  set "NAV2_SLAM_PARAMS=-p base_frame:=base_link"
  for %%A in (%*) do (
    if /i "%%~A"=="--params-file" set "NAV2_SLAM_PARAMS="
    echo(%%~A| "%SystemRoot%\System32\findstr.exe" /i /c:"base_frame" >nul
    if not errorlevel 1 set "NAV2_SLAM_PARAMS="
  )
)

rem /scan and /tf have to be in shared memory before the node activates, so
rem start the daemon when it is down and wait for its first topic sync.
call "%~dp0wait_bridge_ready.bat" %*
if errorlevel 1 exit /b 1

:run
"%SLAM%" %NAV2_SLAM_PARAMS% %*
exit /b %ERRORLEVEL%
