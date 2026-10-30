#!/usr/bin/env bash
set -euo pipefail

source /usr/local/Ascend/ascend-toolkit/set_env.sh

echo "==> Building dvpp_camera_producer..."
g++ -std=c++17 -O2 -Wall -Wextra dvpp_camera_producer.cpp -o dvpp_camera_producer \
  -I/usr/local/Ascend/ascend-toolkit/latest/include \
  -L/usr/local/Ascend/ascend-toolkit/latest/lib64 \
  -L/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64 \
  -Wl,-rpath,/usr/local/Ascend/ascend-toolkit/latest/lib64 \
  -Wl,-rpath,/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64 \
  -lacl_dvpp \
  -lascendcl \
  -lrt

echo "OK: ./dvpp_camera_producer"
