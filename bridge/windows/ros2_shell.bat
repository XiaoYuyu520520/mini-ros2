@echo off
rem ---------------------------------------------------------------------------
rem ros2_shell.bat [start-directory] - open a ready-to-use PowerShell window.
rem
rem This is the handover between the two halves of the Windows side. A .bat
rem cannot set variables in the PowerShell session that called it: cmd runs as a
rem child process and takes its environment with it when it exits, which is why
rem   E:\...\bridge\windows\ros2.bat
rem typed in PowerShell only printed the usage. So this script does the one
rem thing a bat *can* do - hand the job to ros2.ps1 and let it prepare a window:
rem
rem     * ros2.ps1 is dot-sourced, importing PATH and every ROS2_* / NAV2_*
rem       value out of bridge_env.bat into that window,
rem     * so `ros2 topic list -t`, `ros2 topic echo /scan --count 3` and
rem       `ros2 bridge status` work there with no further setup, and
rem     * the bridge daemon still starts on demand, on the first `ros2` command.
rem
rem For the window you are already sitting in, dot-source ros2.ps1 yourself:
rem
rem     . E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1
rem
rem In cmd.exe nothing needs preparing - `ros2.bat topic list -t` carries its own
rem environment - so this script is only interesting from PowerShell.
rem
rem The environment stays temporary: closing the window leaves Windows as it was.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions

rem Where the new window should start. Defaults to where this one is.
set "ROS2_SHELL_START=%~1"
if not defined ROS2_SHELL_START set "ROS2_SHELL_START=%CD%"

rem PowerShell 7 first, Windows PowerShell as the fallback.
set "ROS2_SHELL_PS=pwsh.exe"
where pwsh.exe >nul 2>nul || set "ROS2_SHELL_PS=powershell.exe"

if not exist "%~dp0ros2.ps1" (
    echo [ros2_shell] "%~dp0ros2.ps1" is missing - this script has no job without it 1>&2
    exit /b 1
)

rem `start "title" /D directory program args...` - the title is what keeps the
rem quoted program path from being taken for the window title, and /D is how the
rem new window is put in the right directory without quoting games inside the
rem PowerShell command line.
start "ROS2 bridge" /D "%ROS2_SHELL_START%" "%ROS2_SHELL_PS%" -NoExit -Command ". '%~dp0ros2.ps1'; Write-Host '[ros2_shell] bridge environment loaded. Try: ros2 topic list -t  |  ros2 bridge status' -ForegroundColor Green"

if errorlevel 1 (
    echo [ros2_shell] could not start %ROS2_SHELL_PS% 1>&2
    exit /b 1
)
echo [ros2_shell] opened %ROS2_SHELL_PS% in "%ROS2_SHELL_START%" with the bridge environment loaded.
exit /b 0
