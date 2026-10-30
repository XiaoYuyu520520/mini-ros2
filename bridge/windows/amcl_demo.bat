@echo off
rem ---------------------------------------------------------------------------
rem amcl_demo.bat - localize with nav2_amcl on the Windows side of the bridge.
rem
rem     amcl_demo.bat                            /odom seeds the starting pose
rem     amcl_demo.bat --initial-pose 1.5,-2,0    x,y,yaw in radians
rem     amcl_demo.bat -p laser_max_range:=12.0 --duration 120
rem
rem The cross-platform twin of this script is bridge\scripts\run_amcl_demo.py,
rem which runs on Linux too: the same presets, the same --initial-pose rule.
rem Change one, change the other.
rem
rem It calls bridge_env.bat first, so PATH, the shared-memory geometry and the
rem gRPC SDK config live in this cmd session only - close the window and Windows
rem is back to normal.
rem
rem What it needs in shared memory is /scan, /map and /tf, which the bridge
rem daemon mirrors in from the Ubuntu robot:
rem
rem     ros2_bridge.bat start
rem
rem AMCL publishes the corrected pose on /amcl_pose and the `map` -> `odom`
rem transform on /tf. Unless --initial-pose says otherwise it starts from the
rem pose on /odom, so a robot that is switched on where its odometry puts it
rem needs no arguments at all.
rem
rem Upstream's own `amcl` binary only spins - a lifecycle manager drives
rem configure/activate in a normal ROS2 deployment and there is none on this
rem side of the bridge. amcl_bridge_node does both transitions itself.
rem
rem How well it tracks is worth checking rather than assuming:
rem
rem     build\amcl_probe.exe --duration 60
rem
rem compares /amcl_pose against /odom both at the newest sample and at the
rem instant AMCL stamped its estimate, which is what separates a wrong estimate
rem from a stale one.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
call "%~dp0bridge_env.bat"

set "AMCL=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\build\amcl_bridge_node.exe"
if defined NAV2_AMCL_EXE set "AMCL=%NAV2_AMCL_EXE%"

if not exist "%AMCL%" (
  echo [amcl_demo] %AMCL% not found 1>&2
  echo [amcl_demo] build it with:  cmake --build nav2\ros2_shim\build 1>&2
  exit /b 1
)

rem nav2_amcl defaults to the `base_footprint` robot frame while the bundled sim
rem publishes `base_link`, and its default laser_max_range of 100 m turns a beam
rem that saw nothing into a plausible 100 m return - the simulated lidar reports
rem range_max 12 m, so match it. Extra parameters go through NAV2_AMCL_PARAMS.
rem
rem The presets step aside when the caller says something about them: any
rem argument mentioning the robot frame (`base_frame_id`), or a `--params-file`
rem that brings its own. Parameters for everything else - `laser_max_range`,
rem `save_pose_rate`, ... - leave them alone, which is what keeps a plain
rem `-p max_beams:=60` from costing the caller the frame preset. An explicit
rem NAV2_AMCL_PARAMS always applies.
if not defined NAV2_AMCL_PARAMS (
  set "NAV2_AMCL_PARAMS=-p base_frame_id:=base_link -p laser_max_range:=12.0 -p save_pose_rate:=0.0 -p first_map_only:=true"
  for %%A in (%*) do (
    if /i "%%~A"=="--params-file" set "NAV2_AMCL_PARAMS="
    echo(%%~A| "%SystemRoot%\System32\findstr.exe" /i /c:"base_frame" >nul
    if not errorlevel 1 set "NAV2_AMCL_PARAMS="
  )
)

rem Start from /odom unless the caller supplies a pose of their own.
set "POSE_ARG=--initial-pose-from-odom"
for %%A in (%*) do (
  if /i "%%~A"=="--initial-pose" set "POSE_ARG="
)

rem /scan, /map and /tf have to be in shared memory before AMCL activates, so
rem start the daemon when it is down and wait for its first topic sync.
call "%~dp0wait_bridge_ready.bat" %*
if errorlevel 1 exit /b 1

:run
"%AMCL%" %NAV2_AMCL_PARAMS% %POSE_ARG% %*
exit /b %ERRORLEVEL%
