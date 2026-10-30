#!/bin/bash

echo "========== ROS Car 强制清理与复位 =========="

echo "[1/9] 尝试发送停车指令..."

if [ -f /opt/ros/humble/setup.bash ]; then
  source /opt/ros/humble/setup.bash
fi

# 同时发两条链路：
#   /cmd_vel     停导航链（会被转发程序转成 /car_cmd_vel）
#   /car_cmd_vel 直接停硬件链（SI 单位：m/s + rad/s，0 -> 1500/1500）
# 转发程序失效时 /cmd_vel 到不了底盘，因此 /car_cmd_vel 必须直发。
for i in {1..3}; do
  timeout 1 ros2 topic pub --once /cmd_vel geometry_msgs/msg/Twist \
    "{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}" >/dev/null 2>&1 || true
  timeout 1 ros2 topic pub --once /car_cmd_vel geometry_msgs/msg/Twist \
    "{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}" >/dev/null 2>&1 || true
  sleep 0.2
done

echo "[2/9] 关闭 Nav2 lifecycle 节点..."

for node in \
  /controller_server \
  /planner_server \
  /behavior_server \
  /bt_navigator \
  /waypoint_follower \
  /velocity_smoother \
  /map_server \
  /amcl \
  /lifecycle_manager_navigation
do
  timeout 2 ros2 lifecycle set "$node" shutdown >/dev/null 2>&1 || true
done

sleep 1

echo "[3/9] 温和终止 ROS 相关进程..."

PATTERNS=(
  "nav2"
  "racecar"
  "encoder"
  "encoder_node"
  "imu_encoder_mix"
  "odom_tf"
  "component_container"
  "component_container_isolated"
  "lifecycle_manager"
  "controller_server"
  "planner_server"
  "bt_navigator"
  "behavior_server"
  "waypoint_follower"
  "velocity_smoother"
  "map_server"
  "amcl"
  "slam_toolbox"
  "static_transform_publisher"
  "lslidar"
  "hipnuc"
  "rviz2"
)

for pattern in "${PATTERNS[@]}"; do
  pkill -INT -f "$pattern" 2>/dev/null || true
done

sleep 0.1

echo "[4/9] 强制终止残留进程..."

for pattern in "${PATTERNS[@]}"; do
  pkill -TERM -f "$pattern" 2>/dev/null || true
done

sleep 0.1

for pattern in "${PATTERNS[@]}"; do
  pkill -KILL -f "$pattern" 2>/dev/null || true
done

sleep 0.1

echo "[5/9] 兜底杀 ros2 launch / ros2 run..."

pkill -KILL -f "ros2 launch" 2>/dev/null || true
pkill -KILL -f "ros2 run" 2>/dev/null || true
pkill -KILL -f "python3.*launch" 2>/dev/null || true

sleep 0.1

echo "[6/9] 清理 FastDDS / FastRTPS 共享内存残留..."

rm -f /dev/shm/fastrtps_* 2>/dev/null || true
rm -f /dev/shm/fastdds_* 2>/dev/null || true
rm -f /dev/shm/sem.fastrtps_* 2>/dev/null || true
rm -f /dev/shm/sem.fastdds_* 2>/dev/null || true

# 有些版本会生成这些名字
rm -f /dev/shm/*fastrtps* 2>/dev/null || true
rm -f /dev/shm/*fastdds* 2>/dev/null || true

echo "[7/9] 释放串口占用..."

SERIAL_PORTS=(
  /dev/ttyUSB0
  /dev/ttyUSB1
  /dev/ttyUSB2
  /dev/ttyUSB3
  /dev/ttyACM0
  /dev/ttyACM1
  /dev/ttyACM2
  /dev/ttyACM3
)

for port in "${SERIAL_PORTS[@]}"; do
  if [ -e "$port" ]; then
    echo "释放 $port"
    fuser -k "$port" >/dev/null 2>&1 || true
  fi
done

sleep 0.5

echo "[8/9] 重启 ROS daemon..."

ros2 daemon stop >/dev/null 2>&1 || true
sleep 0.5
ros2 daemon start >/dev/null 2>&1 || true

echo "[9/9] 检查残留..."

echo "--- ROS 相关残留进程 ---"
ps aux | grep -E "ros2|nav2|racecar|encoder|odom_tf|component_container|lifecycle|amcl|slam|lslidar|hipnuc" | grep -v grep || echo "未发现明显残留进程"

echo "--- FastDDS 共享内存残留 ---"
ls /dev/shm | grep -E "fastrtps|fastdds" || echo "未发现 FastDDS / FastRTPS 残留"

echo "========== 清理完成，可以重新启动 =========="
