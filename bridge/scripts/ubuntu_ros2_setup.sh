#!/usr/bin/env bash
# Put a *real* ROS2 on the Ubuntu robot.
#
# Why this exists: every cross-machine number in bridge/docs was measured
# against `ros2_bridge_sim`, a Python peer that speaks the proto but is not
# ROS2. `RclcppBackend` (gRPC/ros2_gRPC_sdk, -DROS2_GRPC_SDK_WITH_ROS2=ON) had
# never been compiled anywhere, so "the bridge can join a real ROS2 graph" was
# an untested claim. This script is the missing half: a Jazzy install on the
# robot that the same gRPC server can link against.
#
#   python bridge/scripts/ubuntu_ssh.py --file bridge/scripts/ubuntu_ros2_setup.sh --quiet
#
# Idempotent: re-running is a no-op once the distro is present. Uses the
# Tsinghua mirror first (packages.ros.org is slow from here) and falls back to
# the upstream repo if the mirror is unreachable.
set -euo pipefail

DISTRO=${ROS2_DISTRO:-jazzy}
CODENAME=$(. /etc/os-release && echo "${VERSION_CODENAME}")
KEYRING=/usr/share/keyrings/ros-archive-keyring.gpg
LIST=/etc/apt/sources.list.d/ros2.list

# ROS2's own setup.bash reads variables it never sets, so `set -u` has to be
# off for the duration of the source -- the same flag that catches our typos
# would otherwise turn a working install into a one-line error.
source_ros() {
  set +u
  # shellcheck disable=SC1091
  . "/opt/ros/${DISTRO}/setup.bash"
  set -u
}

if [ -f "/opt/ros/${DISTRO}/setup.bash" ]; then
  echo "[ros2-setup] /opt/ros/${DISTRO} already installed"
  source_ros
  echo "[ros2-setup] ROS_DISTRO=${ROS_DISTRO} rclcpp=$(ls /opt/ros/${DISTRO}/lib/librclcpp.so 2>/dev/null || echo missing)"
  exit 0
fi

echo "[ros2-setup] codename=${CODENAME} distro=${DISTRO}"

SUDO=""
if [ "$(id -u)" != "0" ]; then SUDO="sudo"; fi

$SUDO apt-get update -qq

# gnupg + curl are what the key fetch needs and what a minimal cloud image
# does not always carry.
$SUDO apt-get install -y -qq ca-certificates curl gnupg >/dev/null

fetch_key() {
  for url in "$@"; do
    echo "[ros2-setup] key <- ${url}"
    if curl -fsSL --max-time 30 "$url" -o /tmp/ros.key 2>/dev/null; then
      if gpg --dearmor < /tmp/ros.key > /tmp/ros.gpg 2>/dev/null; then
        $SUDO install -m 0644 /tmp/ros.gpg "${KEYRING}"
        return 0
      fi
    fi
  done
  return 1
}

fetch_key \
  "https://mirrors.tuna.tsinghua.edu.cn/ros2/ros.key" \
  "https://raw.githubusercontent.com/ros/rosdistro/master/ros.key"

# The mirror and the upstream repo are the same archive; the mirror is only a
# cache, so a failure to reach it is not fatal -- try upstream next.
write_list() {
  local host="$1"
  echo "deb [arch=$(dpkg --print-architecture) signed-by=${KEYRING}] ${host} ${CODENAME} main" \
    > /tmp/ros2.list
  $SUDO install -m 0644 /tmp/ros2.list "${LIST}"
  if $SUDO apt-get update -qq 2>/dev/null; then return 0; fi
  return 1
}

write_list "https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu" \
  || write_list "http://packages.ros.org/ros2/ubuntu"

echo "[ros2-setup] apt install ros-${DISTRO}-ros-base ros-dev-tools (this takes a few minutes)"
$SUDO apt-get install -y ros-${DISTRO}-ros-base ros-dev-tools

source_ros
echo "[ros2-setup] done: ROS_DISTRO=${ROS_DISTRO}"
python3 -c 'import rclpy; print("[ros2-setup] rclpy ok")'
ls /opt/ros/${DISTRO}/lib/librclcpp.so
which colcon rosdep
