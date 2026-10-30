set -o pipefail
source /opt/ros/jazzy/setup.bash
cd ~/code/CPU/code/Desktop/racecar
rm -rf build install log
echo "--- clean tree ---"
ls
echo "--- colcon build, 6 workers ---"
colcon build --parallel-workers 6 2>&1 | tail -40
echo "CONCOLEXIT=${PIPESTATUS[0]}"
echo "--- install tree ---"
ls install 2>/dev/null
ls install/openslam_gmapping/lib 2>/dev/null
ls install/openslam_gmapping/include/gmapping/log 2>/dev/null
