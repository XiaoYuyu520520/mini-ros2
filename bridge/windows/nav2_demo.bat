@echo off
rem ---------------------------------------------------------------------------
rem nav2_demo.bat - run the nav2 planning/driving demo against the live bridge.
rem
rem     nav2_demo.bat --goal auto
rem     nav2_demo.bat --goal 5,-4 --report run.json
rem     nav2_demo.bat --plan-only
rem
rem It loads bridge_env.bat first, which is what makes the gRPC/shared-memory
rem environment temporary: close the window and Windows is back to normal.
rem
rem The demo needs the gRPC bridge daemon to be running, because that is what
rem mirrors /scan, /odom and /map into shared memory:
rem
rem     ros2_bridge.bat start
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
call "%~dp0bridge_env.bat"

set "DEMO=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\build\nav2_bridge_demo.exe"
if defined NAV2_DEMO_EXE set "DEMO=%NAV2_DEMO_EXE%"

if not exist "%DEMO%" (
  echo [nav2_demo] %DEMO% not found 1>&2
  echo [nav2_demo] build it with:  python bridge\scripts\run_nav2_demo.py --build-only 1>&2
  exit /b 1
)

rem /scan, /odom and /map have to be in shared memory before the demo runs,
rem so start the daemon when it is down and wait for its first topic sync.
call "%~dp0wait_bridge_ready.bat" %*
if errorlevel 1 exit /b 1

:run
"%DEMO%" %*
exit /b %ERRORLEVEL%
