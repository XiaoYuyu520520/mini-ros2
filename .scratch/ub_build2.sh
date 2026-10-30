cd ~/code/CPU/code/Desktop/racecar
echo "== maps dir =="
ls -l src/racecar/maps/
echo "== du =="
du -sh src/racecar/maps src/racecar/map map 2>/dev/null
echo "== build racecar + waypoint_cycle =="
source /opt/ros/jazzy/setup.bash
colcon build --packages-select racecar nav2_waypoint_cycle --parallel-workers 6 2>&1 | tail -25
echo "== install tree =="
ls install/racecar/share/racecar 2>/dev/null
ls install/racecar/lib/racecar 2>/dev/null | head -25
echo "== python import check =="
source install/setup.bash
python3 -c "import nav2_msgs.action, nav2_simple_commander, std_srvs.srv; print('imports ok')" 2>&1 | tail -3
ros2 pkg prefix racecar 2>&1 | tail -2
