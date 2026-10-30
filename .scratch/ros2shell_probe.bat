@echo off
start "ROS2 probe" /D "E:\DesktopData\app\code\CPU\.scratch" "pwsh.exe" -Command ". 'E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1'; ros2 topic list -t > 'E:\DesktopData\app\code\CPU\.scratch\ros2shell_probe_out.txt'"
