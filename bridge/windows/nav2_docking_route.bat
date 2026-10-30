@echo off
rem ---------------------------------------------------------------------------
rem nav2_docking_route.bat - the two nav2 servers that are not part of the
rem planning and driving chain, brought up next to a running nav2 stack.
rem
rem     nav2_docking_route.bat                 start both, activate them, stay up
rem     nav2_docking_route.bat --duration 60   exit after a minute
rem     nav2_docking_route.bat stop            stop both
rem
rem The cross-platform twin of this script is
rem bridge\scripts\run_nav2_docking_route.py: the same two servers, on Windows
rem and on Linux. Change one, change the other.
rem
rem `nav2_stack.bat` runs the chain a robot needs to be driven to a goal. These
rem two are upstream nav2 packages that this port compiles and links, but that
rem nothing in the chain depends on, so they are started on their own here - the
rem same arrangement `nav2_safety.bat` uses for the velocity smoother and the
rem collision monitor. Neither is in the stack's `lifecycle_manager.node_names`
rem and the stack's own behaviour is left exactly as it was.
rem
rem     docking_server   DockRobot / UndockRobot, on `/docking_server`
rem     route_server     ComputeRoute / ComputeAndTrackRoute, on `/route_server`
rem
rem Neither has a caller in this repository yet, and neither ships the file it
rem would need to do real work (a dock description, a route graph). What this
rem script establishes is what has to hold before a caller is worth writing:
rem both nodes reach ACTIVE, and every plugin they instantiate while
rem configuring - `opennav_docking::SimpleChargingDock`, and the route planner
rem with its edge scorers and operations - is present in the port's plugin
rem database. A plugin the port cannot find is a configure failure, so "active"
rem is the whole test.
rem
rem Both are lifecycle nodes and neither is driven by the stack's
rem lifecycle_manager, so this script configures and activates them itself, over
rem the same `/<node>/change_state` service the manager would use.
rem
rem Logs: %NAV2_DOCKING_ROUTE_LOG_DIR% (one file per node).
rem
rem     ros2_bridge.bat start
rem     nav2_stack.bat                      (in another window)
rem ---------------------------------------------------------------------------

set "DOCKROUTE_DIR=%~dp0"
if /i "%~1"=="stop" goto :do_stop
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-h" goto :usage

setlocal EnableExtensions
call "%DOCKROUTE_DIR%bridge_env.bat"

set "DOCKROUTE_BUILD=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\build"
if not defined NAV2_SHIM_SRC set "NAV2_SHIM_SRC=%ROS2_BRIDGE_CPU_ROOT%\nav2\nav2_extracted\navigation2-1.5.1"
set "DOCKROUTE_PARAMS=%NAV2_SHIM_SRC%\nav2_bringup\params\nav2_params.yaml"
set "DOCKROUTE_OVERRIDES=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\nav2_shim_overrides.yaml"
set "DOCKROUTE_CONFIG=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\nav2_shim_docking_route.yaml"
if not defined NAV2_DOCKING_ROUTE_LOG_DIR set "NAV2_DOCKING_ROUTE_LOG_DIR=%TEMP%\nav2_docking_route"

set "DURATION=0"
:collect
if "%~1"=="" goto :collected
if /i "%~1"=="--duration" (
  set "DURATION=%~2"
  shift
  shift
  goto :collect
)
if /i "%~1"=="--params" (
  set "DOCKROUTE_CONFIG=%~2"
  shift
  shift
  goto :collect
)
echo [nav2_docking_route] unknown option '%~1' 1>&2
goto :usage
:collected

rem A missing executable is worth naming here rather than three log lines later:
rem `--no-configure docking_server route_server` is what builds just these two.
for %%E in (docking_server.exe route_server.exe) do (
  if not exist "%DOCKROUTE_BUILD%\%%E" (
    echo [nav2_docking_route] %DOCKROUTE_BUILD%\%%E is missing - build it first: 1>&2
    echo [nav2_docking_route]   cd %ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim ^&^& cmake --build build -- docking_server route_server 1>&2
    exit /b 1
  )
)

rem Both nodes stamp and look up transforms, so /clock and /tf have to be in
rem shared memory before they activate - and the bridge is what puts them there.
call "%DOCKROUTE_DIR%wait_bridge_ready.bat" %*
if errorlevel 1 exit /b 1

if not exist "%NAV2_DOCKING_ROUTE_LOG_DIR%" mkdir "%NAV2_DOCKING_ROUTE_LOG_DIR%"
del /q "%NAV2_DOCKING_ROUTE_LOG_DIR%\*.log" >nul 2>&1

set "DOCKROUTE_PARAMS_ARGS=--params-file "%DOCKROUTE_PARAMS%" --params-file "%DOCKROUTE_OVERRIDES%" --params-file "%DOCKROUTE_CONFIG%""
if "%DURATION%"=="0" (set "DOCKROUTE_DURATION_ARGS=") else (set "DOCKROUTE_DURATION_ARGS=--duration %DURATION%")

rem A previous run's nodes would hold the same lifecycle service names, and the
rem activation below would then talk to whichever answered first.
call :kill_nodes 0

echo [nav2_docking_route] logs: %NAV2_DOCKING_ROUTE_LOG_DIR%
call :spawn docking_server "docking_server" ""
call :spawn route_server "route_server" ""

rem The manager polls for each node's change_state service before it uses it;
rem here that wait is this loop. Both nodes answer as soon as they are up.
set /a STEPS=0
:wait_nodes
call "%DOCKROUTE_DIR%ros2.bat" service type /docking_server/change_state >nul 2>&1
if not errorlevel 1 (
  call "%DOCKROUTE_DIR%ros2.bat" service type /route_server/change_state >nul 2>&1
  if not errorlevel 1 goto :activate
)
set /a STEPS+=1
if %STEPS% GEQ 20 (
  echo [nav2_docking_route] the two nodes did not come up; see %NAV2_DOCKING_ROUTE_LOG_DIR% 1>&2
  exit /b 1
)
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :wait_nodes

rem The type is spelled the way `ros2 interface list` prints it, with the kind
rem in it. The transition ids are lifecycle_msgs' own: 1 is `configure`, 3 is
rem `activate` (0/2/4 are create/cleanup/deactivate).
:activate
for %%N in (docking_server route_server) do (
  for %%T in (1 3) do (
    call "%DOCKROUTE_DIR%ros2.bat" service call /%%N/change_state lifecycle_msgs/srv/ChangeState transition.id=%%T >nul 2>&1
    if errorlevel 1 (
      echo [nav2_docking_route] could not drive /%%N/change_state transition %%T 1>&2
      echo [nav2_docking_route] see %NAV2_DOCKING_ROUTE_LOG_DIR%\%%N.log 1>&2
      exit /b 1
    )
  )
  echo [nav2_docking_route] %%N is active
)

echo [nav2_docking_route] docking_server:  DockRobot / UndockRobot on /docking_server
echo [nav2_docking_route] route_server:    ComputeRoute / ComputeAndTrackRoute on /route_server
echo [nav2_docking_route] stop them with:  nav2_docking_route.bat stop

rem A `--duration` run waits here and then takes the nodes down with it; the
rem default leaves them running, because the servers live in the two background
rem processes and there is nothing left for this window to do. The wait is a
rem labelled loop rather than a parenthesised `if` block: cmd resolves `goto`
rem targets before a block runs, and a label inside the block is not one of them
rem ("0 was unexpected at this time").
if "%DURATION%"=="0" exit /b 0
set /a WAIT_S=%DURATION%
:wait_done
if %WAIT_S% LEQ 0 goto :do_stop
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
set /a WAIT_S-=1
goto :wait_done

rem ---------------------------------------------------------------------------
rem :spawn <log name> <title> <extra args>
rem ---------------------------------------------------------------------------
:spawn
set "SPAWN_EXE=%DOCKROUTE_BUILD%\%~1.exe"
set "SPAWN_LOG=%NAV2_DOCKING_ROUTE_LOG_DIR%\%~2.log"
echo [nav2_docking_route]   %~1 -^> %SPAWN_LOG%
start "nav2 %~2" /min cmd /c ""%SPAWN_EXE%" %DOCKROUTE_PARAMS_ARGS% %DOCKROUTE_DURATION_ARGS% %~3 > "%SPAWN_LOG%" 2>&1"
exit /b 0

rem ---------------------------------------------------------------------------
rem :kill_nodes <quiet>
rem ---------------------------------------------------------------------------
:kill_nodes
set "KILL_QUIET=%~1"
set "KILLED="
for %%E in (docking_server.exe route_server.exe) do (
  "%SystemRoot%\System32\tasklist.exe" /fi "imagename eq %%E" 2>nul | "%SystemRoot%\System32\find.exe" /i "%%E" >nul
  if not errorlevel 1 (
    set "KILLED=1"
    "%SystemRoot%\System32\taskkill.exe" /im %%E /f >nul 2>&1
  )
)
if defined KILLED (
  if not "%KILL_QUIET%"=="0" echo [nav2_docking_route] stopping the previous servers
  "%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
)
exit /b 0

:do_stop
setlocal EnableExtensions
call :kill_nodes 1
echo [nav2_docking_route] stopped
exit /b 0

:usage
echo usage: nav2_docking_route.bat [--duration S] [--params FILE]
echo        nav2_docking_route.bat stop
echo.
echo Starts docking_server and route_server next to a running nav2 stack and
echo activates them. Needs the bridge up for /clock and /tf.
exit /b 0
