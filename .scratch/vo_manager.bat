@echo off
call "E:\DesktopData\app\code\CPU\bridge\windows\bridge_env.bat"
"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\lifecycle_manager.exe" --params-file "E:\DesktopData\app\code\CPU\.scratch\vo_lifecycle.yaml" --duration 26
exit /b %ERRORLEVEL%
