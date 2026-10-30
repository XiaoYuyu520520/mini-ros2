echo "== apt update =="
sudo -n env DEBIAN_FRONTEND=noninteractive apt-get update -qq 2>&1 | tail -3
echo "== install =="
sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq ros-jazzy-nav2-amcl ros-jazzy-nav2-msgs ros-jazzy-nav2-simple-commander ros-jazzy-nav2-bringup ros-jazzy-nav2-common ros-jazzy-slam-toolbox 2>&1 | tail -25
echo "== exit $? =="
for p in nav2_amcl nav2_msgs nav2_simple_commander slam_toolbox nav2_bringup nav2_common; do
  if [ -d /opt/ros/jazzy/share/$p ]; then echo "yes $p"; else echo "NO  $p"; fi
done
