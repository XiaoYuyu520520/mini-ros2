set -u
echo "=== needed packages ==="
for p in nav2_amcl message_filters tf2_geometry_msgs tf2_ros tf2 ament_cmake_python action_msgs rclpy nav2_msgs slam_toolbox; do
  if [ -d "/opt/ros/jazzy/share/$p" ]; then echo "yes  $p"; else echo "NO   $p"; fi
done
echo "=== nav2 packages installed ==="
ls /opt/ros/jazzy/share | grep -c '^nav2'
echo "=== tools ==="
command -v colcon || echo "no colcon"
command -v cmake || echo "no cmake"
command -v ninja || echo "no ninja"
