for p in rclcpp rclpy nav_msgs sensor_msgs tf2 tf2_ros tf2_geometry_msgs message_filters geometry_msgs std_msgs visualization_msgs action_msgs std_srvs launch launch_ros ament_index_python ament_cmake nav2_amcl nav2_msgs nav2_simple_commander slam_toolbox nav2_bringup nav2_common nav2_waypoint_cycle; do
  if [ -d /opt/ros/jazzy/share/$p ]; then echo "yes $p"; else echo "NO  $p"; fi
done
echo "== code =="
ls ~/code/CPU 2>/dev/null | head
echo "== sim =="
cat ~/.ros2_bridge_sim.pid 2>/dev/null || echo no-pidfile
