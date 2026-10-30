#!/bin/bash
# 初始化 udev 规则。规则文件位于工程目录：
#   14_串口配置/rules.d/car.rules
#   14_串口配置/rules.d/laser.rules
#   14_串口配置/rules.d/imu.rules
#   14_串口配置/rules.d/encoder.rules
# 若规则文件缺失，必须报错并以非 0 退出，避免"复制失败却返回成功"。
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RULE_SOURCE_DIR="${SCRIPT_DIR}/14_串口配置/rules.d"
RULES_DIR="/etc/udev/rules.d"
RULES=("car.rules" "laser.rules" "imu.rules" "encoder.rules")

missing=0
for rule in "${RULES[@]}"; do
  if [ ! -f "${RULE_SOURCE_DIR}/${rule}" ]; then
    echo "错误：缺少 udev 规则文件 ${RULE_SOURCE_DIR}/${rule}" >&2
    missing=1
  fi
done

if [ "${missing}" -ne 0 ]; then
  echo "初始化失败：请先把缺失的 .rules 文件放到 ${RULE_SOURCE_DIR}/" >&2
  exit 1
fi

for rule in "${RULES[@]}"; do
  sudo cp "${RULE_SOURCE_DIR}/${rule}" "${RULES_DIR}/" || exit 1
done

sudo udevadm control --reload-rules || true
sudo udevadm trigger || true
echo "udev 规则已安装到 ${RULES_DIR}/"
exit 0
