set -u
echo "== sudo -n =="
sudo -n true 2>&1 && echo SUDO_OK || echo SUDO_NEEDS_PASSWORD
echo "== ros2 pkg count =="
source /opt/ros/jazzy/setup.bash >/dev/null 2>&1
ros2 pkg list 2>/dev/null | wc -l
echo "== have? =="
for p in rclpy nav_msgs nav2_amcl nav2_msgs nav2_simple_commander slam_toolbox nav2_bringup; do
  if [ -d /opt/ros/jazzy/share/$p ]; then echo "  $p yes"; else echo "  $p NO"; fi
done
echo "== apt cache for nav2 =="
apt-cache policy ros-jazzy-nav2-amcl 2>/dev/null | head -3
echo "== ~/code/CPU =="
ls ~/code/CPU
echo "== sim pid =="
cat ~/.ros2_bridge_sim.pid 2>/dev/null; ss -lnt 2>/dev/null | grep 50051 || echo no-50051
