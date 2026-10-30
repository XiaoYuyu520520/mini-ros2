#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# ros2.sh - use ROS2 on Linux without installing ROS2.
#
#     bash bridge/linux/ros2.sh topic list -t
#     bash bridge/linux/ros2.sh topic echo /scan --count 5
#     bash bridge/linux/ros2.sh topic pub /cmd_vel geometry_msgs/msg/Twist "linear.x=0.2"
#     bash bridge/linux/ros2.sh topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.2}}"
#
# The value of pub / call / send_goal is one YAML document, the way real ROS2 takes it -
# flow (`{linear: {x: 0.5}}`) or block (`linear:` then an indented `x: 0.5` line); the flat
# `field.path=value` form above keeps working, and all of them end up as the same field
# assignments - see bridge/docs/02-running.md.
#
# The Linux half of bridge\windows\ros2.bat: one command, its own environment, no
# ROS2 anywhere. It also makes sure the gRPC bridge daemon is running and has
# finished its first scan, so the first call performs the fast LAN sweep, opens
# the persistent channel to the agent and mirrors every remote topic into shared
# memory; later calls reuse the running daemon.
#
# Working in a shell instead? Source the environment once and then use `ros2`
# directly - that is what keeps the variables in the session you are in:
#
#     source bridge/linux/bridge_env.sh
#     ros2 topic list -t
#
# Useful overrides (set before calling):
#     ROS2_BRIDGE_AGENT=192.168.1.113:50051   skip discovery, connect directly
#     ROS2_BRIDGE_AUTOSTART=0                 never start the daemon
#     ROS2_BRIDGE_WAIT_MS=5000                budget for the first sync, in ms
#     ROS2_BRIDGE_DAEMON_ARGS=--mirror /scan  extra daemon arguments
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/bridge/linux/bridge_env.sh"

CLI="$ROS2_BRIDGE_BUILD/ros2"
if [ ! -x "$CLI" ]; then
  echo "[ros2] $CLI not found" >&2
  echo "[ros2] build it with:  python3 bridge/scripts/build_bridge.py" >&2
  exit 1
fi

# Wait for the daemon to be up *and* to have mirrored the agent's topics: a cold
# `topic list` would otherwise race its own LAN scan and show nothing but the
# local topics.
if ! bash "$ROOT/bridge/linux/wait_bridge_ready.sh"; then
  exit 1
fi

rc=0
"$CLI" "$@" || rc=$?

# No arguments means the CLI just printed its usage. Say what to do about it,
# because running this with no arguments is what a shell usually tries first.
if [ "$#" -eq 0 ]; then
  echo
  echo "[ros2] No arguments, so that was the command list. Add one, e.g."
  echo "[ros2]     bash bridge/linux/ros2.sh topic list -t"
  echo "[ros2] To keep the environment in the shell you are in, source it once:"
  echo "[ros2]     source bridge/linux/bridge_env.sh"
  echo "[ros2] ... and then simply:  ros2 topic list -t"
fi
exit "$rc"
