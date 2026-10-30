@echo off
rem ---------------------------------------------------------------------------
rem wait_bridge_ready.bat [args...] - make sure the gRPC bridge daemon is up
rem and done with its first scan before the caller does anything.
rem
rem ros2.bat and the nav2 / slam / amcl demo scripts all need the mirrored
rem topics (/scan, /odom, /map, /tf, ...) to be in shared memory before they
rem run a command or activate a node. The daemon announces exactly that moment
rem in %ROS2_BRIDGE_READY_FILE%, so this script polls for the file instead of
rem sleeping for a fixed moment and hoping the LAN scan finished in time.
rem
rem Overrides (set before calling):
rem     ROS2_BRIDGE_AUTOSTART=0          do not start the daemon, do not wait
rem     ROS2_BRIDGE_WAIT_MS=N            budget for the wait (0 = do not wait)
rem     ROS2_BRIDGE_READY_FILE=          no ready file, so no waiting
rem
rem Exit codes: 0 = ready (or nothing to wait for), 1 = daemon start failed.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
call "%~dp0bridge_env.bat"

if /i "%ROS2_BRIDGE_AUTOSTART%"=="0" exit /b 0
if /i "%~1"=="--help" exit /b 0
if /i "%~1"=="-h" exit /b 0

rem Two header lines precede the table; with no matching task tasklist prints a
rem single INFO line, which `skip=2` discards.
set "RUNNING="
for /f "skip=2 tokens=1" %%P in ('tasklist /fi "imagename eq ros2_bridge_daemon.exe" 2^>nul') do set "RUNNING=1"

rem A ready file from a daemon that has since been stopped would pass for this
rem one being up already, so drop it before starting a fresh daemon.
if defined ROS2_BRIDGE_READY_FILE if not defined RUNNING del /f /q "%ROS2_BRIDGE_READY_FILE%" >nul 2>&1

if defined RUNNING goto :wait
echo [ros2_bridge] starting the bridge daemon
call "%~dp0ros2_bridge.bat" start
if errorlevel 1 exit /b 1

:wait
set /a ROS2_BRIDGE_WAIT_MS=%ROS2_BRIDGE_WAIT_MS% 2>nul
if not defined ROS2_BRIDGE_WAIT_MS set "ROS2_BRIDGE_WAIT_MS=30000"
rem Without a ready file there is nothing to poll for, so do not pretend to.
if not defined ROS2_BRIDGE_READY_FILE set "ROS2_BRIDGE_WAIT_MS=0"
set /a STEPS=(%ROS2_BRIDGE_WAIT_MS% + 999) / 1000
set /a STEP=0
if %STEPS% LEQ 0 exit /b 0

:wait_loop
if exist "%ROS2_BRIDGE_READY_FILE%" exit /b 0
set /a STEP+=1
if %STEP% GEQ 3 if not defined SLOW (
  set "SLOW=1"
  echo [ros2_bridge] waiting for the daemon to mirror the agent topics, see %ROS2_BRIDGE_LOG% 1>&2
)
if %STEP% GEQ %STEPS% (
  echo [ros2_bridge] warning: no ready file after %ROS2_BRIDGE_WAIT_MS% ms; running the command anyway 1>&2
  exit /b 0
)
rem `ping -n 1 -w MS` does not sleep - loopback answers instantly - so the
rem pacing is one second per step (ping -n 2 spaces its packets one second).
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :wait_loop
