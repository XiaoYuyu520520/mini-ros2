#!/usr/bin/env bash
# Control the Ubuntu side of the bridge: the simulated robot + gRPC agent.
#
#   bash bridge/scripts/ubuntu_sim_ctl.sh start [--port 50051] [--goal X,Y]
#   bash bridge/scripts/ubuntu_sim_ctl.sh stop
#   bash bridge/scripts/ubuntu_sim_ctl.sh status
#   bash bridge/scripts/ubuntu_sim_ctl.sh log
#
# Intended to be piped in from Windows, which avoids every quoting problem:
#
#   ssh -i <key> ubuntu@192.168.1.113 'bash -s' -- start < bridge/scripts/ubuntu_sim_ctl.sh
set -euo pipefail

ROOT="${ROS2_SIM_ROOT:-$HOME/code/CPU}"
PY="$ROOT/.venv/bin/python"
SIM_DIR="$ROOT/bridge/ubuntu"
LOG="${ROS2_SIM_LOG:-$HOME/sim.log}"
PIDFILE="$HOME/.ros2_bridge_sim.pid"

action="${1:-status}"
shift || true

port=50051
extra=()
while [ $# -gt 0 ]; do
  case "$1" in
    --port) port="$2"; shift 2 ;;
    --goal) extra+=(--goal "$2"); shift 2 ;;
    *) extra+=("$1"); shift ;;
  esac
done

running() {
  [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null
}

case "$action" in
  start)
    if running; then
      echo "already running (pid $(cat "$PIDFILE"))"
    else
      cd "$SIM_DIR"
      args=(--no-console --port "$port")
      if [ "${#extra[@]}" -gt 0 ]; then args+=("${extra[@]}"); fi
      setsid nohup "$PY" -m ros2_bridge_sim.run_sim "${args[@]}" >"$LOG" 2>&1 </dev/null &
      echo $! >"$PIDFILE"
      echo "started pid $! port $port log $LOG"
    fi
    for _ in $(seq 1 40); do
      if ss -lnt 2>/dev/null | grep -q ":$port "; then break; fi
      sleep 0.25
    done
    ss -lnt 2>/dev/null | grep ":$port " || echo "WARNING: nothing listening on $port"
    ;;
  stop)
    if running; then
      kill "$(cat "$PIDFILE")" 2>/dev/null || true
      rm -f "$PIDFILE"
      echo "stopped"
    else
      pkill -f ros2_bridge_sim.run_sim 2>/dev/null && echo "stopped (pkill)" || echo "not running"
    fi
    ;;
  status)
    if running; then
      echo "running (pid $(cat "$PIDFILE"))"
    else
      echo "not running"
    fi
    ss -lnt 2>/dev/null | grep -E ':(50051|[0-9]+) ' | head -5 || true
    ;;
  log)
    tail -n "${2:-40}" "$LOG"
    ;;
  *)
    echo "usage: $0 {start|stop|status|log} [--port N] [--goal X,Y] [--subscribe TOPIC[:TYPE]]" >&2
    exit 2
    ;;
esac