@echo off
rem ---------------------------------------------------------------------------
rem ros2_bridge.bat - control the gRPC <-> shared-memory bridge daemon.
rem
rem     ros2_bridge.bat start  [extra daemon options]
rem     ros2_bridge.bat run    [extra daemon options]   foreground, Ctrl+C stops
rem     ros2_bridge.bat status
rem     ros2_bridge.bat stop
rem
rem `start` puts the daemon in the background and it appends its output to
rem %ROS2_BRIDGE_LOG% (stderr goes to %ROS2_BRIDGE_ERR%); past
rem %ROS2_BRIDGE_LOG_MAX% bytes a log is rotated to <path>.1. Everything is
rem temporary: the environment (PATH, the NAV2_SHM_* slot geometry, the SDK
rem config location) lives in this cmd session only and disappears when the
rem window closes.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
call "%~dp0bridge_env.bat"

set "DAEMON=%ROS2_BRIDGE_BUILD%\ros2_bridge_daemon.exe"
set "CLI=%ROS2_BRIDGE_BUILD%\ros2.exe"

if not exist "%DAEMON%" (
  echo [ros2_bridge] %DAEMON% not found 1>&2
  echo [ros2_bridge] build it with:  python bridge\scripts\build_bridge.py 1>&2
  exit /b 1
)

set "ACTION="
set "EXTRA="
:collect
if "%~1"=="" goto :collected
if not defined ACTION (
  set "ACTION=%~1"
) else (
  set "EXTRA=%EXTRA% %~1"
)
shift
goto :collect
:collected
if not defined ACTION set "ACTION=status"

if defined ROS2_BRIDGE_AGENT set "EXTRA=%EXTRA% --agent %ROS2_BRIDGE_AGENT%"
rem Which file tells a waiting shell that the first topic sync is done; both
rem sides read the path from ROS2_BRIDGE_READY_FILE (bridge_env.bat).
if defined ROS2_BRIDGE_READY_FILE set EXTRA=%EXTRA% --ready-file "%ROS2_BRIDGE_READY_FILE%"
if defined ROS2_BRIDGE_DAEMON_ARGS set "EXTRA=%EXTRA% %ROS2_BRIDGE_DAEMON_ARGS%"

rem Two header lines precede the table; with no matching task tasklist prints a
rem single INFO line, which `skip=2` discards.
set "RUNNING="
for /f "skip=2 tokens=1" %%P in ('tasklist /fi "imagename eq ros2_bridge_daemon.exe" 2^>nul') do set "RUNNING=1"

if /i "%ACTION%"=="run"    goto :do_run
if /i "%ACTION%"=="status" goto :do_status
if /i "%ACTION%"=="stop"   goto :do_stop
if /i "%ACTION%"=="start"  goto :do_start
if /i "%ACTION%"=="ensure" goto :do_start

echo usage: ros2_bridge.bat {start^|run^|status^|stop} [extra daemon options] 1>&2
exit /b 2

:do_run
echo [ros2_bridge] running daemon in the foreground%EXTRA%
call "%DAEMON%"%EXTRA%
exit /b %ERRORLEVEL%

:do_status
if defined RUNNING (
  echo [ros2_bridge] daemon running, log: %ROS2_BRIDGE_LOG%
) else (
  echo [ros2_bridge] daemon not running, log: %ROS2_BRIDGE_LOG%
)
"%CLI%" bridge status
exit /b 0

:do_stop
if not defined RUNNING (
  echo [ros2_bridge] daemon not running
  exit /b 0
)
echo [ros2_bridge] stopping daemon
"%SystemRoot%\System32\taskkill.exe" /im ros2_bridge_daemon.exe /f >nul

rem taskkill answers before the process is really gone. A caller that lists
rem topics right afterwards - `ros2_bridge.bat stop` followed by `ros2.bat topic
rem list` is exactly what acceptance_check.py does - would still see the dying
rem daemon in tasklist, skip its own autostart, and read an empty graph. So wait
rem for it to disappear; the loop is one second per step and normally exits on
rem the first check.
set /a TRIES=0
:do_stop_wait
"%SystemRoot%\System32\tasklist.exe" /fi "imagename eq ros2_bridge_daemon.exe" 2>nul | "%SystemRoot%\System32\find.exe" /i "ros2_bridge_daemon.exe" >nul
if errorlevel 1 goto :do_stop_clean
set /a TRIES+=1
if %TRIES% GEQ 20 (
  echo [ros2_bridge] warning: the daemon is still running after 20 s 1>&2
  exit /b 0
)
"%SystemRoot%\System32\ping.exe" -n 2 127.0.0.1 >nul
goto :do_stop_wait

:do_stop_clean
rem Nothing is mirroring the agent any more, so the ready file must not claim
rem otherwise to a shell that starts the bridge again later.
if defined ROS2_BRIDGE_READY_FILE del /f /q "%ROS2_BRIDGE_READY_FILE%" >nul 2>&1
exit /b 0

:do_start
if defined RUNNING (
  echo [ros2_bridge] daemon already running
  exit /b 0
)
echo [ros2_bridge] starting daemon%EXTRA%
rem `start /b` does not wait for the daemon to finish, and the daemon detaches
rem itself right at startup (--detach): it appends to %ROS2_BRIDGE_LOG% and
rem %ROS2_BRIDGE_ERR% and drops the console it inherited. Releasing those
rem handles matters, because a caller that redirects this script's output -
rem `cmd /c "ros2.bat topic list | more"` - would otherwise block until the
rem daemon exits.
start "ros2_bridge_daemon" /b "%DAEMON%"%EXTRA% --detach
if errorlevel 1 (
  echo [ros2_bridge] could not start the daemon 1>&2
  echo [ros2_bridge] run it in its own window instead:  ros2_bridge.bat run 1>&2
  exit /b 1
)
exit /b 0
