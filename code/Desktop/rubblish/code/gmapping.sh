#!/bin/bash

# Function to handle termination
terminate() {
  echo "终止所有后台进程..."
  for pid in "$Run_car" "$slam_gmapping_pid"; do
    if [ -n "$pid" ]; then
      kill "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
    fi
  done
  echo "所有进程已终止。"
  exit 0
}

# Trap Ctrl+C (SIGINT)
trap terminate SIGINT


# 启动 ROS 2 launch 文件

echo "启动 ROS 2 launch 文件: Run_car.launch.py"
ros2 launch racecar Run_car.launch.py &
Run_car=$!

# 等待5秒
sleep 5


# 启动 slam_gmapping 节点
 echo "启动 slam_gmapping 节点"
 ros2 launch slam_gmapping slam_gmapping.launch.py &
 slam_gmapping_pid=$!

echo "所有节点已启动"

#Wait for all background processes to complete
wait $Run_car  $slam_gmapping_pid