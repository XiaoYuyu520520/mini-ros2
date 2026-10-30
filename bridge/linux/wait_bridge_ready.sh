#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# wait_bridge_ready.sh [--help] - make sure the bridge daemon is up and has
# finished its first scan before the caller does anything.
#
#     bash bridge/linux/wait_bridge_ready.sh
#
# ros2.sh and the nav2 / slam / amcl demo scripts all need the mirrored topics
# (/scan, /odom, /map, /tf, ...) to be in shared memory before they run a command
# or activate a node. The daemon announces exactly that moment by creating
# $ROS2_BRIDGE_READY_FILE, so this polls for the file instead of sleeping for a
# fixed moment and hoping the LAN scan finished in time.
#
# Overrides (set before calling, or before sourcing bridge_env.sh):
#     ROS2_BRIDGE_AUTOSTART=0    do not start the daemon, do not wait
#     ROS2_BRIDGE_WAIT_MS=N      budget for the wait, in ms (0 = do not wait)
#     ROS2_BRIDGE_READY_FILE=    no ready file, so no waiting
#
# Exit codes: 0 = ready (or nothing to wait for), 1 = the daemon did not start.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/bridge/linux/bridge_env.sh"

if [ "${ROS2_BRIDGE_AUTOSTART:-1}" = "0" ]; then
  exit 0
fi
case "${1:-}" in
  --help|-h) exit 0 ;;
esac

running=0
if bash "$ROOT/bridge/linux/ros2_bridge.sh" pid >/dev/null 2>&1; then
  running=1
fi

# A ready file left behind by a daemon that has since been stopped would pass
# for this one being up already, so drop it before starting a fresh daemon.
if [ -n "${ROS2_BRIDGE_READY_FILE:-}" ] && [ "$running" -eq 0 ]; then
  rm -f "$ROS2_BRIDGE_READY_FILE"
fi

if [ "$running" -eq 0 ]; then
  echo "[ros2_bridge] starting the bridge daemon"
  bash "$ROOT/bridge/linux/ros2_bridge.sh" start
fi

# Without a ready file there is nothing to poll for, so do not pretend to.
if [ -z "${ROS2_BRIDGE_READY_FILE:-}" ]; then
  exit 0
fi

budget="${ROS2_BRIDGE_WAIT_MS:-30000}"
case "$budget" in
  ''|*[!0-9]*)
    echo "[ros2_bridge] warning: ROS2_BRIDGE_WAIT_MS='$budget' is not a number; waiting 30 s" >&2
    budget=30000
    ;;
esac
steps=$(( (budget + 999) / 1000 ))
if [ "$steps" -le 0 ]; then
  exit 0
fi

step=0
while [ "$step" -lt "$steps" ]; do
  if [ -f "$ROS2_BRIDGE_READY_FILE" ]; then
    exit 0
  fi
  step=$(( step + 1 ))
  if [ "$step" -eq 3 ]; then
    echo "[ros2_bridge] waiting for the daemon to mirror the agent topics, see $ROS2_BRIDGE_LOG" >&2
  fi
  sleep 1
done
echo "[ros2_bridge] warning: no ready file after ${budget} ms; running the command anyway" >&2
exit 0
