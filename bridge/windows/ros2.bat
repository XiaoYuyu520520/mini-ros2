@echo off
rem ---------------------------------------------------------------------------
rem ros2.bat - the single entry point for using ROS2 on Windows.
rem
rem     ros2 topic list
rem     ros2 topic echo /scan --count 5
rem     ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "linear.x=0.2"
rem     ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.2}}"
rem
rem The value of pub / call / send_goal is one YAML document, the way real ROS2 takes
rem it - flow ({linear: {x: 0.5}}) or block (a `key:` line with the children indented
rem under it). The flat `field.path=value` form above keeps working. All of them end up
rem as the same field assignments, and a value that cannot be represented is an error
rem rather than a different message - see docs/02-running.md.
rem
rem Everything is temporary: the environment lives in this cmd session only.
rem
rem `ros2.bat` also makes sure the gRPC bridge daemon is running and has
rem finished its first scan, so the first call performs the fast LAN sweep,
rem opens the persistent HTTP/2 channel to the Ubuntu agent and mirrors every
rem remote topic into shared memory. Later calls reuse the running daemon.
rem
rem From PowerShell this works too - `ros2.bat topic list -t` carries its own
rem environment into ros2.exe. What PowerShell cannot get from a .bat is the
rem environment *of the session*: cmd is a child process, so its variables die
rem with it. For that either dot-source ros2.ps1 in the window you are in, or let
rem ros2_shell.bat open a prepared one.
rem
rem Useful overrides (set before calling):
rem     ROS2_BRIDGE_AGENT=192.168.1.113:50051   skip discovery, connect directly
rem     ROS2_BRIDGE_AUTOSTART=0                 never start the daemon
rem     ROS2_BRIDGE_WAIT_MS=5000                budget for the first sync (ms)
rem     ROS2_BRIDGE_DAEMON_ARGS=--mirror /scan  extra daemon arguments
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
call "%~dp0bridge_env.bat"

rem Wait for the daemon to be up *and* to have mirrored the agent's topics: a
rem cold `ros2 topic list` would otherwise race its LAN scan and show nothing
rem but the local topics. The daemon writes the ready file this waits for.
call "%~dp0wait_bridge_ready.bat"
if errorlevel 1 exit /b 1

:run
"%ROS2_BRIDGE_BUILD%\ros2.exe" %*
set "ROS2_RC=%ERRORLEVEL%"

rem No arguments means the CLI just printed its usage. Say what to do about it,
rem because "ros2.bat" on its own is what a PowerShell session usually tries
rem first, and the answer there is not "this window has no environment" but
rem "this entry point is for cmd".
if "%~1"=="" (
    echo.
    echo [ros2] No arguments, so that was the command list. Add one, e.g.
    echo [ros2]     ros2.bat topic list -t
    echo [ros2] In PowerShell that already works as-is. To keep the variables in
    echo [ros2] the session you are in, dot-source the PowerShell front end:
    rem %~dp0 ends in a backslash, so this prints wherever the repo actually is.
    echo [ros2]     . %~dp0ros2.ps1
    echo [ros2] ... or ask for a window that has it all loaded already:
    echo [ros2]     ros2_shell.bat
)
exit /b %ROS2_RC%
