#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# connector.sh - 接线器：把三段链路一次接好
#
#   真 ROS2 (Ubuntu)  <--gRPC-->  接线器  <--gRPC-->  仿真 (Ubuntu)
#                                  |
#                                  +--gRPC-->  Windows 守护进程 -> 共享内存 -> nav2
#
# 为什么需要它
# ------------
# `ros2_grpc_sdk_agent --backend rclcpp`（真 ROS2 图）和 `ros2_bridge_sim`
# （仿真图）是**两个各自独立的 gRPC 服务端**，协议一样但图不通：
#
#   * 仿真图只活在 Python 进程的 `graph.Graph` 里，从没进过 DDS；
#   * 真 ROS2 图在 DDS 上，仿真 agent 看不见它。
#
# 接线器就是一个**中继**：它同时是 gRPC 客户端和 gRPC 服务端。
# 对上游（真 ROS2 agent、仿真 agent）它订阅，对下游（Windows 守护进程）它发布。
# 图合并之后，Windows 侧只看到一个 agent，守护进程**一行都不用改**
# （`bridge_daemon.cpp` 是单 peer 模型，见 connectToAgent()）。
#
# 端口约定（全部可用环境变量改）
#   ROS2_LINK_SIM_PORT     仿真 agent            默认 50051
#   ROS2_LINK_REAL_PORT    真 ROS2 agent         默认 50111
#   ROS2_LINK_PORT         接线器对外（给 Windows）默认 50060
#
# 回环 vs 跨机
# -------------
# 本机开发（仿真和真 ROS2 都在这台 Ubuntu 上）走回环：接线器连
# 127.0.0.1，Windows 连 <ubuntu-ip>:50060。跨机时把 ROS2_LINK_HOST 指向
# 本机对外地址即可，接线器本身同时监听回环和对外地址，所以**同一份配置
# 在本机和跨机之间只差一个地址**——这是本脚本存在的意义之一。
#
# 用法
#   bash start/connector.sh up       启动接线器（前台，Ctrl+C 停）
#   bash start/connector.sh up -d    后台启动
#   bash start/connector.sh down     停
#   bash start/connector.sh status   看状态
#   bash start/connector.sh check    只验证两端 agent 是否活着
#
# Windows 侧怎么连
#   set ROS2_BRIDGE_AGENT=<ubuntu-ip>:50060
#   bridge\windows\ros2_bridge.bat start
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="${CPU_ROOT:-$HOME/code/CPU}"
# 本仓库在 Ubuntu 上的位置（也允许 CPU_ROOT 直接指到 bridge/ubuntu 那一层）
SIM_DIR="$ROOT/bridge/ubuntu"
PY="${ROS2_LINK_PY:-$ROOT/.venv/bin/python}"

SIM_PORT="${ROS2_LINK_SIM_PORT:-50051}"
REAL_PORT="${ROS2_LINK_REAL_PORT:-50111}"
LINK_PORT="${ROS2_LINK_PORT:-50060}"

# 两边都发布同一个话题时怎么办（/scan、/odom、/tf、/clock 通常都撞）：
#   prefer-sim   仿真保住原名（/scan），真 ROS2 的改挂 /real/scan
#   prefer-real  真 ROS2 保住原名，仿真的改挂 /sim/scan
# 两种都**不丢数据**，只是不让两台机器的数据绞在一起。
COLLISION="${ROS2_LINK_COLLISION:-prefer-sim}"

# 接线器对外要能被 Windows 看到，所以绑 0.0.0.0；但**它连上游时**用回环，
# 因为上游两个 agent 都在本机。这样本机开发不依赖任何网卡地址。
UPSTREAM_HOST="${ROS2_LINK_UPSTREAM_HOST:-127.0.0.1}"

RUN_DIR="${ROS2_LINK_RUN_DIR:-$HOME/.ros2_link}"
PIDFILE="$RUN_DIR/connector.pid"
LOGFILE="$RUN_DIR/connector.log"
mkdir -p "$RUN_DIR"

action="${1:-status}"
shift || true

# ---------------------------------------------------------------------------
# 辅助
# ---------------------------------------------------------------------------

# 端口有没有人在听。用来判断「上游 agent 起来了没有」——Ubuntu 上不一定
# 有 ss/netstat，所以先试 ss 再退到 python。
port_open() {
  local port="$1"
  if command -v ss >/dev/null 2>&1; then
    ss -lnt 2>/dev/null | grep -q ":$port " && return 0 || return 1
  fi
  "$PY" - "$port" <<'PYEOF' 2>/dev/null
import socket, sys
s = socket.socket()
s.settimeout(0.5)
try:
    s.connect(("127.0.0.1", int(sys.argv[1])))
except OSError:
    sys.exit(1)
finally:
    s.close()
sys.exit(0)
PYEOF
}

connector_pid() {
  [ -f "$PIDFILE" ] || return 1
  local pid
  pid="$(cat "$PIDFILE" 2>/dev/null || true)"
  [ -n "$pid" ] || return 1
  kill -0 "$pid" 2>/dev/null || return 1
  printf '%s\n' "$pid"
}

require_python() {
  if [ ! -x "$PY" ]; then
    echo "[connector] python not found: $PY" >&2
    echo "[connector] set ROS2_LINK_PY, or create the venv:" >&2
    echo "[connector]     python3 -m venv $ROOT/.venv" >&2
    exit 1
  fi
}

# ---------------------------------------------------------------------------
# up
# ---------------------------------------------------------------------------
do_up() {
  require_python
  if pid="$(connector_pid)"; then
    echo "[connector] already running (pid $pid), port $LINK_PORT"
    return 0
  fi

  echo "[connector] upstream: sim  127.0.0.1:$SIM_PORT"
  echo "[connector] upstream: real 127.0.0.1:$REAL_PORT"
  echo "[connector] serving : 0.0.0.0:$LINK_PORT  (Windows connects here)"
  echo "[connector] collision: $COLLISION  (同名话题谁保住原名)"

  local args=(
    --sim "$UPSTREAM_HOST:$SIM_PORT"
    --real "$UPSTREAM_HOST:$REAL_PORT"
    --listen "0.0.0.0:$LINK_PORT"
    --loopback-listen "127.0.0.1:$LINK_PORT"
    --collision "$COLLISION"
    --log "$LOGFILE"
  )
  # 额外参数原样转给接线器本体
  if [ "${#extra[@]}" -gt 0 ]; then args+=("${extra[@]}"); fi

  local cmd="$PY -m ros2_link.relay ${args[*]}"
  echo "[connector] $cmd"

  if [ "$detach" = "1" ]; then
    # setsid 让接线器活过调用它的 shell
    (
      cd "$SIM_DIR"
      PYTHONPATH="$SIM_DIR:${PYTHONPATH:-}" \
        setsid nohup "$PY" -m ros2_link.relay "${args[@]}" >"$LOGFILE" 2>&1 </dev/null &
      echo $! >"$PIDFILE"
    )
    sleep 0.5
    if ! connector_pid >/dev/null; then
      echo "[connector] failed to start; log:" >&2
      tail -n 30 "$LOGFILE" >&2 || true
      exit 1
    fi
    echo "[connector] started pid $(cat "$PIDFILE"), log $LOGFILE"
  else
    cd "$SIM_DIR"
    PYTHONPATH="$SIM_DIR:${PYTHONPATH:-}" exec "$PY" -m ros2_link.relay "${args[@]}"
  fi
}

# ---------------------------------------------------------------------------
# down / status / check
# ---------------------------------------------------------------------------
do_down() {
  if ! pid="$(connector_pid)"; then
    echo "[connector] not running"
    rm -f "$PIDFILE"
    return 0
  fi
  echo "[connector] stopping pid $pid"
  kill "$pid" 2>/dev/null || true
  for _ in $(seq 1 50); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.1
  done
  kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null || true
  rm -f "$PIDFILE"
  echo "[connector] stopped"
}

do_status() {
  if pid="$(connector_pid)"; then
    echo "[connector] running (pid $pid), serving 0.0.0.0:$LINK_PORT"
  else
    echo "[connector] not running"
  fi
  for name_port in "sim:$SIM_PORT" "real:$REAL_PORT" "link:$LINK_PORT"; do
    name="${name_port%%:*}"; port="${name_port##*:}"
    if port_open "$port"; then
      echo "  $name ($port): LISTENING"
    else
      echo "  $name ($port): -"
    fi
  done
}

do_check() {
  rc=0
  for name_port in "sim:$SIM_PORT" "real:$REAL_PORT"; do
    name="${name_port%%:*}"; port="${name_port##*:}"
    if port_open "$port"; then
      echo "[connector] $name agent on $port: UP"
    else
      echo "[connector] $name agent on $port: DOWN" >&2
      rc=1
    fi
  done
  if port_open "$LINK_PORT"; then
    echo "[connector] relay on $LINK_PORT: UP"
  else
    echo "[connector] relay on $LINK_PORT: DOWN" >&2
    rc=1
  fi
  exit "$rc"
}

# ---------------------------------------------------------------------------
# 参数
# ---------------------------------------------------------------------------
detach=0
extra=()
while [ $# -gt 0 ]; do
  case "$1" in
    -d|--detach) detach=1; shift ;;
    --sim|--real|--listen|--loopback-listen|--log) extra+=("$1" "$2"); shift 2 ;;
    *) extra+=("$1"); shift ;;
  esac
done

case "$action" in
  up|start) do_up ;;
  down|stop) do_down ;;
  restart) do_down; do_up ;;
  status) do_status ;;
  check) do_check ;;
  *)
    echo "usage: connector.sh {up|down|restart|status|check} [-d] [extra args]" >&2
    exit 2
    ;;
esac
