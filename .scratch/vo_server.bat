@echo off
call "E:\DesktopData\app\code\CPU\bridge\windows\bridge_env.bat"
"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\vector_object_server.exe" --params-file "E:\DesktopData\app\code\CPU\nav2\nav2_extracted\navigation2-1.5.1\nav2_map_server\params\vector_object_server_params.yaml" -p use_sim_time:=true --duration 30
exit /b %ERRORLEVEL%
