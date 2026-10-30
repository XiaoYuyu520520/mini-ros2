set -e
cd ~/code/CPU
echo "== toolchain =="
nproc; cmake --version | head -1; g++ --version | head -1
echo "== build =="
python3 bridge/scripts/build_bridge.py 2>&1 | tail -22
echo "== ctest =="
ctest --test-dir bridge/windows/build --output-on-failure 2>&1 | tail -20
echo "== roundtrip directly =="
./bridge/windows/build/bridge_service_roundtrip | tail -3
