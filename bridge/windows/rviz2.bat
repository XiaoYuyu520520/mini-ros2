@echo off
rem ---------------------------------------------------------------------------
rem rviz2 - start the Windows visualiser against the shared-memory graph.
rem
rem Load the environment first (or use a shell that did):
rem
rem     call bridge\windows\bridge_env.bat
rem     rviz2
rem
rem `bridge_env.bat` puts this directory on PATH, so the command is found by
rem name. Everything the visualiser needs - NAV2_SHM_NAMESPACE in particular -
rem is inherited by viz.exe, and viz.exe passes it on to viz_feed.exe.
rem
rem Extra arguments go to the GUI, so `rviz2 -config other.json` works.
rem ---------------------------------------------------------------------------
set "ROS2_VIZ=%~dp0..\..\viz\viz.exe"
if exist "%ROS2_VIZ%" goto run
if defined ROS2_BRIDGE_CPU_ROOT set "ROS2_VIZ=%ROS2_BRIDGE_CPU_ROOT%\viz\viz.exe"
if exist "%ROS2_VIZ%" goto run

echo [rviz2] viz.exe was not found. Build it first:
echo         cd viz ^&^& go build -ldflags "-H windowsgui" -o viz.exe .
exit /b 1

:run
start "mini-ros2 viz" "%ROS2_VIZ%" %*