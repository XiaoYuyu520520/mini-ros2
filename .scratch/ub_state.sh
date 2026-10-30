set -u
echo "== os =="
lsb_release -ds 2>/dev/null || cat /etc/os-release | head -2
echo "== ros =="
ls /opt/ros 2>/dev/null
echo "== ros2 pkg nav2 count =="
ls /opt/ros/*/share 2>/dev/null | grep -c '^nav2' || true
echo "== slam_toolbox =="
ls -d /opt/ros/*/share/slam_toolbox 2>/dev/null || echo none
echo "== colcon =="
which colcon || echo no-colcon
echo "== python =="
python3 -V
echo "== cpu =="
nproc
echo "== mem =="
free -g | head -2
echo "== disk =="
df -h /home | tail -1
echo "== code dir =="
ls -d ~/code/CPU 2>/dev/null || echo no-code-CPU
echo "== net =="
timeout 8 curl -sI https://repo.ros2.org 2>/dev/null | head -1 || echo no-net
