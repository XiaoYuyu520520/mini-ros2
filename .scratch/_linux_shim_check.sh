cd ~/code/CPU || exit 1
echo "== shim build entry point (Linux)"
python3 nav2/ros2_shim/build.py > /tmp/shim_build.log 2>&1
build_rc=$?
echo "build rc=$build_rc"
tail -n 8 /tmp/shim_build.log
if [ $build_rc -ne 0 ]; then
  echo "--- last errors ---"
  grep -i -m 20 -E "error|fatal" /tmp/shim_build.log
  exit $build_rc
fi
echo "== shim ctest (Linux)"
ctest --test-dir nav2/ros2_shim/build --output-on-failure > /tmp/shim_ctest.log 2>&1
ctest_rc=$?
grep -E "tests passed|Failed|Total Test time" /tmp/shim_ctest.log | tail -n 12
echo "ctest rc=$ctest_rc"
exit $ctest_rc
