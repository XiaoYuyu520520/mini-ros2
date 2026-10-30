@echo off
rem ---------------------------------------------------------------------------
rem nav2_safety.bat - the two nodes that sit between the controller and the
rem wheels, brought up next to a running nav2 stack.
rem
rem     nav2_safety.bat                 start both, activate them, stay up
rem     nav2_safety.bat --duration 60   exit after a minute
rem     nav2_safety.bat stop            stop both
rem
rem The cross-platform twin of this script is bridge\scripts\run_nav2_safety.py:
rem the same two nodes, on Windows and on Linux. Change one, change the other.
rem
rem     nav2_stack.bat --goal auto
rem
rem `bridge\windows\nav2_stack.bat` runs the planning and driving chain:
rem controller_server writes `/cmd_vel` and the robot takes its orders from
rem there. The two nodes here are the ones upstream puts *after* the controller
rem in a nav2_bringup launch file, and they are the reason this is a separate
rem script: neither is in the stack's `lifecycle_manager.node_names`, and the
rem stack's chain is left exactly as it was.
rem
rem     velocity_smoother   /cmd_vel          -> /cmd_vel_smoothed
rem     collision_monitor   /cmd_vel_smoothed -> /cmd_vel_monitored
rem
rem What that buys, with no remapping anywhere: the same velocity stream the
rem robot is driving from is also run through the acceleration limits in
rem nav2_shim_safety.yaml, and then checked against `/scan` with a stop polygon
rem 0.45 m ahead of the robot. `/cmd_vel_smoothed` is what the controller's
rem commands look like once they are inside the platform's acceleration
rem envelope; `/cmd_vel_monitored` is what they look like once the laser has
rem had a say. Neither topic has another writer, so both can be run against a
rem live stack without touching what the robot does.
rem
rem Watching is not the only way to run the pair. `nav2_stack.bat --safety`
rem starts these same two nodes *in* the chain instead: the controller and the
rem behavior server are remapped to `/cmd_vel_nav`, the smoother follows them
rem there - nav2_velocity_smoother's topics are hardcoded in its own source
rem (`cmd_vel` in, `cmd_vel_smoothed` out), which is why upstream remaps them
rem from its launch file and why that mode is a flag on the stack script - and
rem the monitor's `cmd_vel_out_topic` goes back to upstream's `cmd_vel`. See
rem nav2_shim_safety_chain.yaml, which is that one parameter.
rem
rem Use this script for looking and that mode for driving: run at once, they
rem would put a second reader on `/cmd_vel_smoothed` and a second writer on the
rem `/cmd_vel` the monitor owns.
rem
rem Both nodes are lifecycle nodes and neither is driven by the stack's
rem lifecycle_manager, so this script configures and activates them itself,
rem over the same `/<node>/change_state` service the manager would use.
rem
rem Logs: %NAV2_SAFETY_LOG_DIR% (one file per node).
rem
rem     ros2_bridge.bat start
rem     nav2_stack.bat                      (in another window)
rem ---------------------------------------------------------------------------

set "SAFETY_DIR=%~dp0"
if /i "%~1"=="stop" goto :do_stop
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-h" goto :usage

setlocal EnableExtensions
call "%SAFETY_DIR%bridge_env.bat"

set "SAFETY_BUILD=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\build"
if not defined NAV2_SHIM_SRC set "NAV2_SHIM_SRC=%ROS2_BRIDGE_CPU_ROOT%\nav2\nav2_extracted\navigation2-1.5.1"
set "SAFETY_PARAMS=%NAV2_SHIM_SRC%\nav2_bringup\params\nav2_params.yaml"
set "SAFETY_OVERRIDES=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\nav2_shim_overrides.yaml"
set "SAFETY_CONFIG=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\nav2_shim_safety.yaml"
if not defined NAV2_SAFETY_LOG_DIR set "NAV2_SAFETY_LOG_DIR=%TEMP%\nav2_safety"

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
  set "SAFETY_CONFIG=%~2"
  shift
  shift
  goto :collect
)
echo [nav2_safety] unknown option '%~1' 1>&2
goto :usage
:collected

for %%E in (velocity_smoother.exe collision_monitor.exe) do (
  if not exist "%SAFETY_BUILD%\%%E" (
    echo [nav2_safety] %SAFETY_BUILD%\%%E is missing - build it first: 1>&2
    echo [nav2_safety]   cd %ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim ^&^& cmake --build build --velocity_smoother collision_monitor 1>&2
    exit /b 1
  )
)

rem /scan and /tf have to be in shared memory before either node activates -
rem the monitor reads the laser and looks up `base_link` in its frame.
call "%SAFETY_DIR%wait_bridge_ready.bat" %*
if errorlevel 1 exit /b 1

if not exist "%NAV2_SAFETY_LOG_DIR%" mkdir "%NAV2_SAFETY_LOG_DIR%"
del /q "%NAV2_SAFETY_LOG_DIR%\*.log" >nul 2>&1

set "SAFETY_PARAMS_ARGS=--params-file "%SAFETY_PARAMS%" --params-file "%SAFETY_OVERRIDES%" --params-file "%SAFETY_CONFIG%""
if "%DURATION%"=="0" (set "SAFETY_DURATION_ARGS=") else (set "SAFETY_DURATION_ARGS=--duration %DURATION%")

rem A previous run's nodes would hold the same topic names and the same
rem lifecycle services; the graph would then have two nodes called
rem `velocity_smoother` and the activation below would talk to whichever
rem answered first.
call :kill_nodes 0

echo [nav2_safety] logs: %NAV2_SAFETY_LOG_DIR%
call :spawn velocity_smoother "velocity_smoother" ""
call :spawn collision_monitor "collision_monitor" ""

rem The manager polls for each node's change_state service before it uses it;
rem here that wait is this loop. Both nodes answer as soon as they are up.
set /a STEPS=0
:wait_nodes
call "%SAFETY_DIR%ros2.bat" service type /velocity_smoother/change_state >nul 2>&1
if not errorlevel 1 (
  call "%SAFETY_DIR%ros2.bat" service type /collision_monitor/change_state >nul 2>&1
  if not errorlevel 1 goto :activate
)
set /a STEPS+=1
if %STEPS% GEQ 20 (
  echo [nav2_safety] the two nodes did not come up; see %NAV2_SAFETY_LOG_DIR% 1>&2
  exit /b 1
)
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :wait_nodes

rem Note the type spelling: `ros2 service call` takes the interface name the way
rem `ros2 interface list` prints it, which is `lifecycle_msgs/srv/ChangeState`
rem with the kind in it. The transition ids are lifecycle_msgs' own: 1 is
rem `configure`, 3 is `activate` (0/2/4 are create/cleanup/deactivate).
:activate
for %%N in (velocity_smoother collision_monitor) do (
  for %%T in (1 3) do (
    call "%SAFETY_DIR%ros2.bat" service call /%%N/change_state lifecycle_msgs/srv/ChangeState transition.id=%%T >nul 2>&1
    if errorlevel 1 (
      echo [nav2_safety] could not drive /%%N/change_state transition %%T 1>&2
      echo [nav2_safety] see %NAV2_SAFETY_LOG_DIR%\%%N.log 1>&2
      exit /b 1
    )
  )
  echo [nav2_safety] %%N is active
)

echo [nav2_safety] velocity_smoother:  /cmd_vel          -^> /cmd_vel_smoothed
echo [nav2_safety] collision_monitor:  /cmd_vel_smoothed -^> /cmd_vel_monitored
echo [nav2_safety] watch them with:
echo [nav2_safety]   ros2.bat topic echo /cmd_vel_smoothed
echo [nav2_safety]   ros2.bat topic echo /cmd_vel_monitored
echo [nav2_safety] stop them with:  nav2_safety.bat stop

rem A `--duration` run waits here and then takes the nodes down with it; the
rem default leaves them running, because the control loop lives in the two
rem background processes and there is nothing left for this window to do.
rem The wait is a labelled loop rather than a parenthesised `if` block: cmd
rem resolves `goto` targets before a block runs, and a label inside the block
rem is not one of them ("0 was unexpected at this time").
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
set "SPAWN_EXE=%SAFETY_BUILD%\%~1.exe"
set "SPAWN_LOG=%NAV2_SAFETY_LOG_DIR%\%~2.log"
echo [nav2_safety]   %~1 -^> %SPAWN_LOG%
start "nav2 %~2" /min cmd /c ""%SPAWN_EXE%" %SAFETY_PARAMS_ARGS% %SAFETY_DURATION_ARGS% %~3 > "%SPAWN_LOG%" 2>&1"
exit /b 0

rem ---------------------------------------------------------------------------
rem :kill_nodes <quiet>
rem ---------------------------------------------------------------------------
:kill_nodes
set "KILL_QUIET=%~1"
set "KILLED="
for %%E in (velocity_smoother.exe collision_monitor.exe) do (
  "%SystemRoot%\System32\tasklist.exe" /fi "imagename eq %%E" 2>nul | "%SystemRoot%\System32\find.exe" /i "%%E" >nul
  if not errorlevel 1 (
    set "KILLED=1"
    "%SystemRoot%\System32\taskkill.exe" /im %%E /f >nul 2>&1
  )
)
if defined KILLED (
  if not "%KILL_QUIET%"=="0" echo [nav2_safety] stopping the previous safety nodes
  "%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
)
exit /b 0

:do_stop
setlocal EnableExtensions
call :kill_nodes 1
echo [nav2_safety] stopped
exit /b 0

:usage
echo usage: nav2_safety.bat [--duration S] [--params FILE]
echo        nav2_safety.bat stop
echo.
echo Starts velocity_smoother and collision_monitor next to a running nav2
echo stack and activates them. Needs the bridge up for /scan, /odom and /tf.
exit /b 0
