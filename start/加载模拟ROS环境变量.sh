#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 加载模拟ROS环境变量.sh - 把「模拟 ROS2」的运行环境装进当前 shell
#
#     source start/加载模拟ROS环境变量.sh
#
# 它是 bridge/linux/bridge_env.sh 的**上位包装**：那个文件给的是兼容层
# （共享内存 ROS2）需要的环境变量，这个文件再补上仿真侧（ros2_bridge_sim /
# ros2_link）需要的那几项，并且**按需把 Ubuntu 真 ROS2 的环境加在前面**——
# 因为真 ROS2 的 `ros2` 命令和兼容层的 `ros2` 命令同名，只能靠 PATH 顺序
# 区分：真 ROS2 在前，兼容层在后面。
#
# 变量
#   ROS2_SIM_PYTHON       仿真用的 python           默认 <root>/.venv/bin/python
#   ROS2_SIM_ROOT         仿真包所在目录            默认 <root>/bridge/ubuntu
#   ROS2_LINK_SIM_PORT    仿真 agent 端口           默认 50051
#   ROS2_LINK_REAL_PORT   真 ROS2 agent 端口        默认 50111
#   ROS2_LINK_PORT        接线器对外端口            默认 50060
#   ROS2_BRIDGE_AGENT     守护进程连谁              默认 127.0.0.1:50060
#   ROS2_REAL_ROS2        是否把真 ROS2 的环境加进来  默认 1（装了 /opt/ros/* 就加）
#
# 全部都是**临时的**：它们活在 source 它的那个 shell 里，退出即消失。
# ---------------------------------------------------------------------------

if [ -z "${BASH_VERSION:-}" ]; then
  echo "加载模拟ROS环境变量.sh needs bash: it resolves its own path through BASH_SOURCE." >&2
  return 1 2>/dev/null || exit 1
fi

# 自身位置 -> 仓库根
_START_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPU_ROOT="${CPU_ROOT:-$(cd "$_START_DIR/.." && pwd)}"
export CPU_ROOT

# 兼容层的环境变量是**唯一事实源**，这里不重复任何取值，只负责加载。
if [ -f "$CPU_ROOT/bridge/linux/bridge_env.sh" ]; then
  . "$CPU_ROOT/bridge/linux/bridge_env.sh"
else
  echo "[sim-env] $CPU_ROOT/bridge/linux/bridge_env.sh not found" >&2
  return 1 2>/dev/null || exit 1
fi

# --- 仿真侧 -----------------------------------------------------------------
: "${ROS2_SIM_ROOT:=$CPU_ROOT/bridge/ubuntu}"
: "${ROS2_SIM_PYTHON:=$CPU_ROOT/.venv/bin/python}"
: "${ROS2_LINK_SIM_PORT:=50051}"
: "${ROS2_LINK_REAL_PORT:=50111}"
: "${ROS2_LINK_PORT:=50060}"

# 仿真包要能被 import 到，所以 PYTHONPATH 得指到 bridge/ubuntu 这一层。
case ":${PYTHONPATH:-}:" in
  *":$ROS2_SIM_ROOT:"*) ;;
  *) PYTHONPATH="$ROS2_SIM_ROOT${PYTHONPATH:+:$PYTHONPATH}" ;;
esac
export PYTHONPATH

# 真 ROS2 的环境：加在**前面**，这样 `ros2 topic list` 走的是真 ROS2，
# 兼容层的 ros2 仍然可用（用 bridge/linux/ros2.sh 那个显式入口）。
# 关掉它：ROS2_REAL_ROS2=0。
: "${ROS2_REAL_ROS2:=1}"
if [ "$ROS2_REAL_ROS2" = "1" ]; then
  for _ros_setup in /opt/ros/jazzy/setup.bash /opt/ros/humble/setup.bash /opt/ros/foxy/setup.bash; do
    if [ -f "$_ros_setup" ]; then
      # 真 ROS2 的 setup.bash 要求 bash，而且自己会设一堆变量，所以用
      # 一个子 shell 求值完再导回来——`source` 直接跑会把 set -u 弄炸。
      set +u
      eval "$(bash -c "set +u; . '$_ros_setup' >/dev/null 2>&1; \
          printf 'ROS_DISTRO=%s\n' \"\${ROS_DISTRO:-}\"; \
          printf 'AMENT_PREFIX_PATH=%s\n' \"\${AMENT_PREFIX_PATH:-}\"; \
          printf 'PYTHONPATH=%s\n' \"\${PYTHONPATH:-}\"; \
          printf 'PATH=%s\n' \"\${PATH:-}\"; \
          printf 'LD_LIBRARY_PATH=%s\n' \"\${LD_LIBRARY_PATH:-}\"")" 2>/dev/null || true
      set -u
      break
    fi
  done
  unset _ros_setup
fi

# 真 ROS2 在前，兼容层的 ros2 在后面。
case ":$PATH:" in
  *":$ROS2_BRIDGE_BUILD:"*) ;;
  *) PATH="$ROS2_BRIDGE_BUILD:$PATH" ;;
esac
export PATH

# --- 默认连到哪里 -----------------------------------------------------------
# 本机开发：接线器在本机，走回环。跨机时改成 <ubuntu-ip>:50060。
: "${ROS2_BRIDGE_AGENT:=127.0.0.1:$ROS2_LINK_PORT}"
export ROS2_BRIDGE_AGENT

export ROS2_SIM_ROOT ROS2_SIM_PYTHON
export ROS2_LINK_SIM_PORT ROS2_LINK_REAL_PORT ROS2_LINK_PORT

echo "[sim-env] CPU_ROOT          = $CPU_ROOT"
echo "[sim-env] ROS2_SIM_ROOT     = $ROS2_SIM_ROOT"
echo "[sim-env] ROS2_BRIDGE_AGENT = $ROS2_BRIDGE_AGENT"
echo "[sim-env] ros2 (compat)     = $ROS2_BRIDGE_BUILD"
echo "[sim-env] now:  ros2 topic list -t   |   bash start/connector.sh up"
