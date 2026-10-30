@echo off
rem ---------------------------------------------------------------------------
rem nav2_stack.bat - bring the whole nav2 stack up on the Windows side of the
rem bridge, in one command.
rem
rem     nav2_stack.bat                          stack only, stays up
rem     nav2_stack.bat --goal auto              then drive the robot to a goal
rem     nav2_stack.bat --goal "3.5,-2" --report g.json --timeout 120
rem                                             (quote a two-number goal:
rem                                             cmd splits an unquoted comma)
rem     nav2_stack.bat --duration 120           exit after two minutes
rem     nav2_stack.bat --no-amcl                no localization (see below)
rem     nav2_stack.bat --map map\ai_map001.yaml  localize against a saved map
rem                                             instead of the bridged one
rem     nav2_stack.bat --safety                 smooth and check the commands
rem                                             on their way to the wheels
rem     nav2_stack.bat --safety --safety-params F.yaml   ... with F loaded last
rem     nav2_stack.bat stop                     stop everything it started
rem
rem The cross-platform twin of this script is bridge\scripts\run_nav2_stack.py:
rem the same options, on Windows and on Linux. Change one, change the other.
rem
rem It calls bridge_env.bat first, so PATH, the shared-memory geometry and the
rem gRPC SDK config live in this cmd session only - close the window and Windows
rem is back to normal.
rem
rem What it starts, and why each piece is needed:
rem
rem     amcl_bridge_node   map -> odom, from /scan and /map
rem     planner_server     global planning (NavFn), publishes /plan
rem     controller_server  path following (RPP), publishes /cmd_vel
rem     bt_navigator       the behavior tree that sequences the two above
rem     behavior_server    spin / wait / back-up, for the tree's recovery branch
rem     smoother_server    path smoothing over the SmoothPath action
rem     waypoint_follower  one NavigateToPose per pose, over FollowWaypoints
rem     lifecycle_manager  drives configure/activate on all of them
rem
rem The order matters twice over. The servers come up *unconfigured* - upstream
rem nav2 expects a launch file to hand them to a lifecycle manager - so a
rem manager is what activates them, and it is also what makes `nav2_goal.exe`
rem usable at all: the BT navigator only accepts a goal once it is active. AMCL
rem goes first because both costmaps resolve `map -> base_link`, and that path
rem only exists once AMCL is publishing the `map -> odom` leg.
rem
rem What it needs in shared memory, which the daemon mirrors in from the robot:
rem /scan, /odom, /map, /tf, /tf_static and /clock.
rem
rem     ros2_bridge.bat start

rem behavior_server is what the recovery leaves of both behavior trees send
rem their goals to. It is not optional: a tree is built when bt_navigator
rem activates, and a BT action node whose server is missing makes the whole
rem activation fail - "Action server spin not available" - so the stack needs
rem it up before the manager reaches bt_navigator, the same way it needs AMCL
rem up before either costmap can resolve `map -> base_link`.
rem
rem Logs of the background nodes are under %NAV2_STACK_LOG_DIR% - one file per
rem node, because five processes writing into one console is unreadable, and
rem because the startup check reads the lifecycle manager's log to know when the
rem stack is actually up rather than sleeping for a fixed moment.
rem
rem --no-amcl is for a machine that already has a `map -> odom` transform of its
rem own - slam_demo.bat running in another window is the usual reason. Without
rem one, nav2_goal fails on its first transform lookup.

rem --map FILE is the other source of that transform, and the twin of the car's
rem `Run_nav.launch.py map:=FILE`: map_server loads the file - a PGM and its YAML
rem sidecar - and is activated here, before AMCL, because AMCL is what turns the
rem map into `map -> odom`. /map has one writer, so this refuses to start when
rem the daemon is still mirroring the peer's map, and prints the two commands
rem that take /map off the mirror set. Under `--map` the stack also needs
rem map_server.exe, which is one more binary from the same build.
rem
rem --safety puts the two nodes upstream runs *after* the controller into the
rem chain itself, so a velocity command reaches the wheels through the
rem acceleration limits in nav2_shim_safety.yaml and past the stop polygon the
rem collision monitor keeps in front of the robot (0.45 m, half a metre wide,
rem against `/scan`):
rem
rem     controller_server   -> /cmd_vel_nav       -r cmd_vel:=cmd_vel_nav
rem     behavior_server     -> /cmd_vel_nav       -r cmd_vel:=cmd_vel_nav
rem     velocity_smoother   -> /cmd_vel_smoothed  -r cmd_vel:=cmd_vel_nav
rem     collision_monitor   -> /cmd_vel           cmd_vel_out_topic
rem
rem That is the shape of upstream's navigation_launch.py, which remaps the same
rem two writers to `cmd_vel_nav` and lets nav2_params.yaml's
rem `cmd_vel_in_topic: cmd_vel_smoothed` / `cmd_vel_out_topic: cmd_vel` join the
rem four up again. The one difference is the monitor's output topic, which this
rem port spells out in nav2_shim_safety_chain.yaml, because it is also the value
rem that has to change back for the beside-the-stack mode nav2_safety.bat runs.
rem
rem Without --safety the chain is exactly what it was: the controller owns
rem /cmd_vel and the robot drives from it. The two extra nodes are not in the
rem lifecycle manager's `node_names` either way, so this script starts and
rem activates them over their own /<node>/change_state services once the
rem manager has reported the rest of the stack active.
rem ---------------------------------------------------------------------------

rem `stop` is handled before the environment is touched: stopping must work from
rem a shell whose environment was never set up, and it needs nothing from it.
set "STACK_DIR=%~dp0"
if /i "%~1"=="stop" goto :do_stop
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-h" goto :usage

setlocal EnableExtensions
call "%STACK_DIR%bridge_env.bat"

set "STACK_BUILD=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\build"
set "STACK_OVERRIDES=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\nav2_shim_overrides.yaml"
set "STACK_SAFETY_CONFIG=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\nav2_shim_safety.yaml"
set "STACK_SAFETY_CHAIN=%ROS2_BRIDGE_CPU_ROOT%\nav2\ros2_shim\config\nav2_shim_safety_chain.yaml"
if not defined NAV2_SHIM_SRC set "NAV2_SHIM_SRC=%ROS2_BRIDGE_CPU_ROOT%\nav2\nav2_extracted\navigation2-1.5.1"
set "STACK_PARAMS=%NAV2_SHIM_SRC%\nav2_bringup\params\nav2_params.yaml"
if not defined NAV2_STACK_LOG_DIR set "NAV2_STACK_LOG_DIR=%TEMP%\nav2_stack"
if not defined NAV2_STACK_WAIT_MS set "NAV2_STACK_WAIT_MS=90000"

set "AMCL=1"
set "SAFETY=0"
set "DURATION=0"
set "SAFETY_PARAMS="
set "MAP_FILE="
set "GOAL_ARGS="

:collect
if "%~1"=="" goto :collected
if /i "%~1"=="--no-amcl" goto :opt_no_amcl
if /i "%~1"=="--safety" goto :opt_safety
if /i "%~1"=="--safety-params" goto :opt_safety_params
if /i "%~1"=="--duration" goto :opt_duration
if /i "%~1"=="--params" goto :opt_params
if /i "%~1"=="--map" goto :opt_map
rem Anything this script does not know is the goal command: it is run against
rem the stack once the stack is active, and nav2_goal owns those options.
set "GOAL_ARGS=%GOAL_ARGS% %~1"
shift
goto :collect

:opt_no_amcl
set "AMCL=0"
shift
goto :collect

:opt_safety
set "SAFETY=1"
shift
goto :collect

rem Loaded *after* nav2_shim_safety_chain.yaml, so it can restate anything in
rem either safety file - the polygons and the velocity limits, most usefully -
rem without editing the repository copies. It implies --safety.
:opt_safety_params
set "SAFETY=1"
set "SAFETY_PARAMS=%~2"
shift
shift
goto :collect

:opt_duration
set "DURATION=%~2"
shift
shift
goto :collect

:opt_params
set "STACK_PARAMS=%~2"
shift
shift
goto :collect

rem The map a run brings itself. Loaded by map_server, which this script starts
rem and activates before AMCL - the same order run_nav2_stack.py uses.
:opt_map
set "MAP_FILE=%~2"
shift
shift
goto :collect

:collected

for %%E in (amcl_bridge_node planner_server controller_server bt_navigator behavior_server smoother_server waypoint_follower lifecycle_manager) do (
  if not exist "%STACK_BUILD%\%%E.exe" (
    echo [nav2_stack] %STACK_BUILD%\%%E.exe not found 1>&2
    echo [nav2_stack] build it with:  cmake --build nav2\ros2_shim\build 1>&2
    exit /b 1
  )
)
if not exist "%STACK_PARAMS%" (
  echo [nav2_stack] parameter file not found: %STACK_PARAMS% 1>&2
  exit /b 1
)
if "%MAP_FILE%"=="" goto :checked
if not exist "%STACK_BUILD%\map_server.exe" (
  echo [nav2_stack] %STACK_BUILD%\map_server.exe not found - it is what --map loads with 1>&2
  echo [nav2_stack] build it with:  cmake --build nav2\ros2_shim\build 1>&2
  exit /b 1
)
if not exist "%MAP_FILE%" (
  echo [nav2_stack] map file not found: %MAP_FILE% 1>&2
  exit /b 1
)

rem --safety adds two more nodes to the command path, so both binaries and both
rem parameter files have to be there before the stack is started at all: a
rem missing smoother would otherwise be discovered when the wheels do not turn.
if not "%SAFETY%"=="1" goto :checked
for %%E in (velocity_smoother.exe collision_monitor.exe) do (
  if not exist "%STACK_BUILD%\%%E" (
    echo [nav2_stack] %STACK_BUILD%\%%E not found - it is part of --safety 1>&2
    echo [nav2_stack] build it with:  cmake --build nav2\ros2_shim\build 1>&2
    exit /b 1
  )
)
for %%F in ("%STACK_SAFETY_CONFIG%" "%STACK_SAFETY_CHAIN%" "%SAFETY_PARAMS%") do (
  if not "%%~F"=="" if not exist "%%~F" (
    echo [nav2_stack] parameter file not found: %%~F 1>&2
    exit /b 1
  )
)
:checked

rem A stack left over from an earlier run would keep its topics and its
rem lifecycle state, and the new manager would then talk to two nodes of the
rem same name. Start from nothing.
call :kill_nodes 0

rem /scan, /map, /tf and /clock have to be in shared memory before the servers
rem activate, so start the daemon when it is down and wait for its first sync.
call "%STACK_DIR%wait_bridge_ready.bat" %*
if errorlevel 1 exit /b 1

if not exist "%NAV2_STACK_LOG_DIR%" mkdir "%NAV2_STACK_LOG_DIR%"

rem Fresh logs, every run. The wait below reads the lifecycle manager's log to
rem learn when the stack is up, and a log left over from the previous run
rem already ends in "Managed nodes are active" - so without this a second run
rem decides the stack is ready the moment it looks, sends its goal before
rem bt_navigator has an action server, and reports "the goal response never
rem arrived" or "the navigator refused the goal" while the stack is in fact
rem still coming up. taskkill answers before the file handle is released, so
rem the delete is retried rather than trusted.
set /a LOG_TRIES=0
:clear_logs
if not exist "%NAV2_STACK_LOG_DIR%\lifecycle_manager.log" goto :logs_clear
del /q "%NAV2_STACK_LOG_DIR%\*.log" >nul 2>&1
set /a LOG_TRIES+=1
if %LOG_TRIES% GEQ 10 (
  echo [nav2_stack] warning: could not delete %NAV2_STACK_LOG_DIR%\*.log 1>&2
  goto :logs_clear
)
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :clear_logs
:logs_clear

set "STACK_PARAMS_ARGS=--params-file "%STACK_PARAMS%" --params-file "%STACK_OVERRIDES%""
if "%DURATION%"=="0" (set "STACK_DURATION_ARGS=") else (set "STACK_DURATION_ARGS=--duration %DURATION%")

echo [nav2_stack] logs: %NAV2_STACK_LOG_DIR%

rem /map has one writer, and --map makes map_server its writer: a daemon that is
rem still mirroring the peer's map would put two maps in one channel, and the
rem costmaps would localise against whichever wrote last. Refusing here is
rem before anything has been started, which is the only cheap moment to refuse.
if "%MAP_FILE%"=="" goto :map_done
call "%STACK_DIR%ros2.bat" bridge status 2>nul | "%SystemRoot%\System32\findstr.exe" /c:"[gRPC bridge]" | "%SystemRoot%\System32\findstr.exe" /c:"/map " >nul
if not errorlevel 1 (
  echo [nav2_stack] the daemon is mirroring /map from the peer, and --map makes map_server its writer 1>&2
  echo [nav2_stack] two writers on one topic are two maps in one channel; restart it without /map: 1>&2
  echo [nav2_stack]   ros2_bridge.bat stop 1>&2
  echo [nav2_stack]   ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan --mirror /tf --mirror /tf_static 1>&2
  exit /b 2
)
echo [nav2_stack] starting map_server  (%MAP_FILE%)
call :spawn map_server "map_server" "-p yaml_filename:=%MAP_FILE%"
set /a STEPS=0
:wait_map
call "%STACK_DIR%ros2.bat" service type /map_server/change_state >nul 2>&1
if not errorlevel 1 goto :activate_map
set /a STEPS+=1
if %STEPS% GEQ 20 (
  echo [nav2_stack] map_server did not come up 1>&2
  echo [nav2_stack] see %NAV2_STACK_LOG_DIR%\map_server.log 1>&2
  exit /b 1
)
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :wait_map
:activate_map
for %%T in (1 3) do (
  call "%STACK_DIR%ros2.bat" service call /map_server/change_state lifecycle_msgs/srv/ChangeState transition.id=%%T >nul 2>&1
  if errorlevel 1 (
    echo [nav2_stack] could not drive /map_server/change_state transition %%T 1>&2
    echo [nav2_stack] see %NAV2_STACK_LOG_DIR%\map_server.log 1>&2
    exit /b 1
  )
)
echo [nav2_stack] map_server -^> /map  ^(it is the only writer of it^)
:map_done

if "%AMCL%"=="1" (
  echo [nav2_stack] starting amcl_bridge_node  ^(map -^> odom^)
  call :spawn amcl_bridge_node "amcl_bridge_node" "-p base_frame_id:=base_link -p laser_max_range:=12.0 -p save_pose_rate:=0.0 -p first_map_only:=true --initial-pose-from-odom"
)

rem The two writers on the command path lose `/cmd_vel` before they start, when
rem --safety is on, because velocity_smoother takes that name over further down.
rem A node's topic names are fixed when it is built, so this is the only moment
rem the move can happen, and upstream does it the same way - `remappings=[(
rem 'cmd_vel', 'cmd_vel_nav')]` on both of these in navigation_launch.py.
if "%SAFETY%"=="1" (set "STACK_CMD_VEL_REMAP=-r cmd_vel:=cmd_vel_nav") else (set "STACK_CMD_VEL_REMAP=")

echo [nav2_stack] starting planner_server, controller_server, bt_navigator, behavior_server, smoother_server, waypoint_follower
call :spawn planner_server "planner_server" ""
call :spawn controller_server "controller_server" "%STACK_CMD_VEL_REMAP%"
call :spawn bt_navigator "bt_navigator" ""
call :spawn behavior_server "behavior_server" "%STACK_CMD_VEL_REMAP%"
call :spawn smoother_server "smoother_server" ""
call :spawn waypoint_follower "waypoint_follower" ""

rem The manager polls for each node's services, so the servers need a moment to
rem announce themselves; a cold start of four 20 Hz servers plus two costmaps is
rem the slowest thing in this script and it is not worth racing.
"%SystemRoot%\System32\ping.exe" -n 5 127.0.0.1 >nul
echo [nav2_stack] starting lifecycle_manager
call :spawn lifecycle_manager "lifecycle_manager" "--params-file "%STACK_OVERRIDES%""

set /a STEPS=(%NAV2_STACK_WAIT_MS% + 999) / 1000
set /a STEP=0
:wait_active
"%SystemRoot%\System32\findstr.exe" /c:"Managed nodes are active" "%NAV2_STACK_LOG_DIR%\lifecycle_manager.log" >nul 2>&1
if not errorlevel 1 goto :active
rem A bringup that has already given up is worth reporting now instead of after
rem the whole budget: the manager says which node failed, and that node's own
rem log says why - a costmap that never saw its transform, most often.
"%SystemRoot%\System32\findstr.exe" /c:"Aborting bringup" /c:"Failed to change state for node:" "%NAV2_STACK_LOG_DIR%\lifecycle_manager.log" >nul 2>&1
if not errorlevel 1 goto :bringup_failed
set /a STEP+=1
if %STEP% GEQ %STEPS% (
  echo [nav2_stack] warning: the stack did not report itself active within %NAV2_STACK_WAIT_MS% ms 1>&2
  echo [nav2_stack] see %NAV2_STACK_LOG_DIR%\lifecycle_manager.log 1>&2
  goto :ready_anyway
)
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :wait_active

:bringup_failed
echo [nav2_stack] the lifecycle manager could not bring the stack up 1>&2
"%SystemRoot%\System32\findstr.exe" /c:"Failed" /c:"ERROR" "%NAV2_STACK_LOG_DIR%\lifecycle_manager.log" 1>&2
echo [nav2_stack] node logs: %NAV2_STACK_LOG_DIR% 1>&2
echo [nav2_stack] stop the leftovers with:  nav2_stack.bat stop 1>&2
exit /b 1

:active
echo [nav2_stack] planner_server, controller_server, bt_navigator, behavior_server, smoother_server and waypoint_follower are active

rem The two --safety nodes go up last, on purpose: the manager has activated the
rem controller and the behavior server by now, so nothing is going to publish on
rem /cmd_vel_nav - or read /cmd_vel - that is not part of the chain, and the
rem smoother's first input is a command the finished stack produced.
if not "%SAFETY%"=="1" goto :no_safety
call :start_safety
if errorlevel 1 exit /b 1
:no_safety

rem A goal sent in the first second after activation can be lost: the shim's
rem action channels are per-process shared-memory rings, and a request written
rem while a server is still attaching to its channels is seen by nobody. What
rem that looks like is not an error - bt_navigator accepts the goal, the tree
rem ticks, and no /cmd_vel ever comes out, because the compute_path_to_pose
rem request the tree sent was never answered, so the goal runs until the
rem caller's timeout. Measured on this machine: two goals sent ~0.1 s after
rem the stack reported itself active hung for their whole 120 s budget, every
rem goal sent a few seconds later (including the same start and goal poses)
rem succeeded. Four seconds of patience is cheap next to a 25 s bringup.
"%SystemRoot%\System32\ping.exe" -n 5 127.0.0.1 >nul

:ready_anyway
if not defined GOAL_ARGS (
  echo [nav2_stack] the stack is running; stop it with:  nav2_stack.bat stop
  exit /b 0
)

rem The goal runs in the foreground: it prints progress, and the JSON report
rem says whether the robot actually moved - which is the thing worth checking,
rem not the exit code.
echo [nav2_stack] nav2_goal%GOAL_ARGS%
"%STACK_BUILD%\nav2_goal.exe"%GOAL_ARGS%
set "GOAL_RC=%ERRORLEVEL%"
if "%DURATION%"=="0" echo [nav2_stack] the stack is still running; stop it with:  nav2_stack.bat stop
exit /b %GOAL_RC%

rem ---------------------------------------------------------------------------
rem :spawn <log name> <title> <extra args>
rem
rem Starts one node in the background with its own log file.
rem
rem `start /min`, not `start /b`: /b shares this console, and a shared console
rem means a shared handle table, so every node inherits whatever this script's
rem stdout happens to be. Run the script interactively and that is the console
rem and nothing is wrong; run it as `nav2_stack.bat --goal auto > run.log` and
rem the caller's own shell waits for the *nodes* to exit before it sees the
rem end of the log. /min gives each node a console of its own, which the log
rem file then takes over - the window stays empty - and the caller is free the
rem moment this script returns.
rem
rem `cmd /c ""exe" args > log 2>&1"` is what redirects the node's own handles:
rem a bare `start /min exe > log` sends the child to its new console instead.
rem
rem The parameter files are named *before* %~3, because that is the order the
rem loader applies them in: nav2_params.yaml has an `amcl` block that says
rem `base_frame_id: base_footprint`, and a `-p base_frame_id:=base_link` placed
rem before it loses to it. What that looks like is AMCL activating and then
rem failing every laser scan with "frame 'base_footprint' does not exist", no
rem `map -> odom` ever appearing, and both servers' costmaps timing out on
rem `map -> base_link` while the lifecycle manager gives up on activating them.
rem ---------------------------------------------------------------------------
:spawn
set "SPAWN_EXE=%STACK_BUILD%\%~1.exe"
set "SPAWN_LOG=%NAV2_STACK_LOG_DIR%\%~2.log"
set "SPAWN_ARGS=%~3"
echo [nav2_stack]   %~1 -^> %SPAWN_LOG%
start "nav2 %~2" /min cmd /c ""%SPAWN_EXE%" %STACK_PARAMS_ARGS% %STACK_DURATION_ARGS% %SPAWN_ARGS% > "%SPAWN_LOG%" 2>&1"
exit /b 0

rem ---------------------------------------------------------------------------
rem :spawn_safety <exe> <log name> <extra args>
rem
rem The same as :spawn, with the two safety parameter files inserted after the
rem stack's own. They are named last because that is the order the loader
rem applies them in - nav2_shim_safety_chain.yaml is what moves the monitor's
rem output onto /cmd_vel, and nav2_params.yaml's `cmd_vel_out_topic:
rem cmd_vel_monitored`-style values would win if it were named first.
rem
rem It is a second label rather than another :spawn argument because the two
rem file paths have to stay quoted, and a quoted string inside a `call :label
rem "..."` argument is re-parsed by cmd into something else entirely.
rem ---------------------------------------------------------------------------
:spawn_safety
set "SPAWN_EXE=%STACK_BUILD%\%~1.exe"
set "SPAWN_LOG=%NAV2_STACK_LOG_DIR%\%~2.log"
echo [nav2_stack]   %~1 -^> %SPAWN_LOG%
start "nav2 %~2" /min cmd /c ""%SPAWN_EXE%" %STACK_PARAMS_ARGS% %STACK_SAFETY_PARAMS_ARGS% %STACK_DURATION_ARGS% %~3 > "%SPAWN_LOG%" 2>&1"
exit /b 0

rem ---------------------------------------------------------------------------
rem :start_safety - start the two in-chain nodes and activate them.
rem
rem Called from :active, once the manager has the rest of the stack up, and only
rem for --safety. Neither node is in the manager's `node_names`, so the
rem configure/activate pair is driven here over the same
rem /<node>/change_state service the manager would use - the same way
rem bridge\windows\nav2_safety.bat brings up the same two nodes.
rem
rem velocity_smoother is the node that needs the remap: its input topic is a
rem literal `cmd_vel` in velocity_smoother.cpp, so `-r cmd_vel:=cmd_vel_nav`
rem is what makes it read the controller's stream instead of the empty name.
rem collision_monitor takes both topics from parameters, so it is started with
rem no remap at all.
rem ---------------------------------------------------------------------------
:start_safety
set "STACK_SAFETY_PARAMS_ARGS=--params-file "%STACK_SAFETY_CONFIG%" --params-file "%STACK_SAFETY_CHAIN%""
if not "%SAFETY_PARAMS%"=="" set "STACK_SAFETY_PARAMS_ARGS=%STACK_SAFETY_PARAMS_ARGS% --params-file "%SAFETY_PARAMS%""
echo [nav2_stack] --safety: starting velocity_smoother and collision_monitor
call :spawn_safety velocity_smoother "velocity_smoother" "-r cmd_vel:=cmd_vel_nav"
call :spawn_safety collision_monitor "collision_monitor" ""

rem Both answer as soon as they are up, so this loop is the manager's own
rem "wait for the service" step, done by hand.
set /a STEPS=0
:wait_safety
call "%STACK_DIR%ros2.bat" service type /velocity_smoother/change_state >nul 2>&1
if not errorlevel 1 (
  call "%STACK_DIR%ros2.bat" service type /collision_monitor/change_state >nul 2>&1
  if not errorlevel 1 goto :activate_safety
)
set /a STEPS+=1
if %STEPS% GEQ 20 (
  echo [nav2_stack] the two --safety nodes did not come up 1>&2
  echo [nav2_stack] see %NAV2_STACK_LOG_DIR%\velocity_smoother.log and collision_monitor.log 1>&2
  echo [nav2_stack] the rest of the stack is up; stop it with:  nav2_stack.bat stop 1>&2
  exit /b 1
)
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :wait_safety

rem Transition ids are lifecycle_msgs' own: 1 is configure, 3 is activate.
:activate_safety
for %%N in (velocity_smoother collision_monitor) do (
  for %%T in (1 3) do (
    call "%STACK_DIR%ros2.bat" service call /%%N/change_state lifecycle_msgs/srv/ChangeState transition.id=%%T >nul 2>&1
    if errorlevel 1 (
      echo [nav2_stack] could not drive /%%N/change_state transition %%T 1>&2
      echo [nav2_stack] see %NAV2_STACK_LOG_DIR%\%%N.log 1>&2
      exit /b 1
    )
  )
  echo [nav2_stack] %%N is active
)
echo [nav2_stack] command chain:  controller_server -^> /cmd_vel_nav
echo [nav2_stack]                  velocity_smoother -^> /cmd_vel_smoothed
echo [nav2_stack]                  collision_monitor -^> /cmd_vel  ^(only writer^)
exit /b 0

rem ---------------------------------------------------------------------------
rem :kill_nodes <quiet> - stop the nodes this script starts.
rem
rem Nine of them, plus the two `--safety` adds.
rem
rem By image name rather than by pid: `start /b` gives no pid, and these are the
rem port's own binaries with names nothing else on the machine uses.
rem ---------------------------------------------------------------------------
:kill_nodes
set "KILL_QUIET=%~1"
set "KILLED="
for %%E in (lifecycle_manager.exe bt_navigator.exe controller_server.exe planner_server.exe behavior_server.exe smoother_server.exe waypoint_follower.exe amcl_bridge_node.exe map_server.exe velocity_smoother.exe collision_monitor.exe) do (
  "%SystemRoot%\System32\tasklist.exe" /fi "imagename eq %%E" 2>nul | "%SystemRoot%\System32\find.exe" /i "%%E" >nul
  if not errorlevel 1 (
    set "KILLED=1"
    "%SystemRoot%\System32\taskkill.exe" /im %%E /f >nul 2>&1
  )
)
rem taskkill answers before the process is gone, and the next thing this script
rem does is start nodes with the same names: a manager that finds a dying
rem server still in the graph is the failure this avoids.
if defined KILLED (
  if not "%KILL_QUIET%"=="0" echo [nav2_stack] stopping the previous stack
  "%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
)
exit /b 0

:do_stop
setlocal EnableExtensions
call :kill_nodes 1
echo [nav2_stack] stopped
exit /b 0

:usage
echo usage: nav2_stack.bat [--safety] [--no-amcl] [--map FILE] [--duration S] [--params FILE] [goal options]
echo        nav2_stack.bat stop
echo.
echo   Any nav2_goal option - --goal X,Y^|auto, --timeout S, --report FILE - is
echo   passed to nav2_goal once the stack is active, e.g.
echo       nav2_stack.bat --goal auto --report goal.json
echo.
echo   A two-number goal has to be quoted: cmd splits an argument on the comma,
echo   so `--goal 3.5,-2` reaches nav2_goal as --goal 3.5 -2 and is rejected with
echo   "--goal wants X,Y or auto, got '3.5'". Write --goal "3.5,-2" instead.
echo.
echo   --map FILE loads that file - a PGM and its YAML sidecar - with map_server
echo   and makes it the stack's /map, the way the car's Run_nav.launch.py takes
echo   map:=FILE. map_server goes up and is activated before AMCL, because AMCL
echo   is what turns the map into map -^> odom. /map has one writer, so the daemon
echo   must not be mirroring the peer's map: this refuses to start when it is, and
echo   prints the commands that take /map off the mirror set.
echo.
echo   --safety puts velocity_smoother and collision_monitor in the command
echo   chain instead of beside it: controller_server and behavior_server are
echo   remapped to /cmd_vel_nav, the smoother publishes /cmd_vel_smoothed and
echo   the monitor is the only writer of /cmd_vel, the topic the robot drives
echo   from. Limits and the stop polygon are in nav2_shim_safety.yaml.
echo.
echo   --safety-params FILE loads one more node parameter file, last, for the
echo   two of them - which is how the polygon can be widened for a test run
echo   without editing nav2_shim_safety.yaml. It implies --safety.
exit /b 0
