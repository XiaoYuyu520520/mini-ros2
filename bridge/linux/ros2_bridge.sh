#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# ros2_bridge.sh - control the gRPC <-> shared-memory bridge daemon on Linux.
#
#     bash bridge/linux/ros2_bridge.sh start  [extra daemon options]
#     bash bridge/linux/ros2_bridge.sh run    [extra daemon options]  foreground
#     bash bridge/linux/ros2_bridge.sh status
#     bash bridge/linux/ros2_bridge.sh stop
#     bash bridge/linux/ros2_bridge.sh pid
#     bash bridge/linux/ros2_bridge.sh log [N]
#
# The Linux half of bridge\windows\ros2_bridge.bat, driving the binary that
# `python3 bridge/scripts/build_bridge.py` produces here:
# bridge/windows/build/ros2_bridge_daemon - the tree keeps its historical name on
# both platforms, the `.exe` does not.
#
# `start` detaches: the daemon reopens its own stdout/stderr onto
# $ROS2_BRIDGE_LOG / $ROS2_BRIDGE_ERR (--detach), and past $ROS2_BRIDGE_LOG_MAX
# bytes a log is rotated to <path>.1. Everything is as temporary as it is on
# Windows - the environment lives in the shell that sourced bridge_env.sh and
# goes away with that shell.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/bridge/linux/bridge_env.sh"

DAEMON="$ROS2_BRIDGE_BUILD/ros2_bridge_daemon"
CLI="$ROS2_BRIDGE_BUILD/ros2"
PIDFILE="$ROS2_BRIDGE_BUILD/ros2_bridge_daemon.pid"
# Linux truncates a process name to 15 bytes, so "ros2_bridge_daemon" is read
# back out of /proc as "ros2_bridge_dae".
COMM="$(basename "$DAEMON" | cut -c1-15)"

# The pid of the daemon when one is alive; nothing, exit 1, when none is.
#
# The pidfile is tried first and the process table second, because `setsid` forks
# when its child is already a process-group leader - so `start` cannot always
# know the pid of what it launched - and the fallback also covers a pidfile
# deleted by hand or left behind by a daemon that has since died.
#
# What is matched is the process *name* (/proc/<pid>/comm), never the command
# line: a `pgrep -f ros2_bridge_daemon` would also match the ssh session that ran
# this script, because its command line contains that very string, and the script
# would then cheerfully stop a daemon that is not there.
daemon_pid() {
  local pid name
  if [ -f "$PIDFILE" ]; then
    pid="$(cat "$PIDFILE" 2>/dev/null || true)"
    if [ -n "$pid" ] && [ "$(cat "/proc/$pid/comm" 2>/dev/null || true)" = "$COMM" ]; then
      printf '%s\n' "$pid"
      return 0
    fi
  fi
  for name in /proc/[0-9]*/comm; do
    [ -r "$name" ] || continue
    if [ "$(cat "$name" 2>/dev/null || true)" = "$COMM" ]; then
      name="${name#/proc/}"
      printf '%s\n' "${name%/comm}"
      return 0
    fi
  done
  return 1
}

if [ ! -x "$DAEMON" ]; then
  echo "[ros2_bridge] $DAEMON not found" >&2
  echo "[ros2_bridge] build it with:  python3 bridge/scripts/build_bridge.py" >&2
  exit 1
fi

action="${1:-status}"
shift || true

# Everything after the action is an option for the daemon, exactly as the .bat collects
# them. Until this line existed they were read and thrown away on the `start` path:
# `ros2_bridge.sh start --mirror /scan` echoed the option in "starting daemon ..."
# and then started a daemon with every topic mirrored, because only `run` handed
# "$@" on. The two scripts have to answer one command the same way - `--map` in
# run_nav2_stack.py prints this very command as the way to take /map off the mirror
# set, and on Linux it did nothing.
extra=("$@")
if [ -n "${ROS2_BRIDGE_AGENT:-}" ]; then
  extra+=(--agent "$ROS2_BRIDGE_AGENT")
fi
# Which file tells a waiting shell that the first topic sync is done; both sides
# read the path from ROS2_BRIDGE_READY_FILE (bridge_env.sh).
if [ -n "${ROS2_BRIDGE_READY_FILE:-}" ]; then
  extra+=(--ready-file "$ROS2_BRIDGE_READY_FILE")
fi
# Word-split on purpose: this is the same "extra arguments" hatch the .bat has,
# and quoting it would turn ROS2_BRIDGE_DAEMON_ARGS="--mirror /scan" into one
# argument the daemon cannot parse.
if [ -n "${ROS2_BRIDGE_DAEMON_ARGS:-}" ]; then
  # shellcheck disable=SC2206
  extra+=($ROS2_BRIDGE_DAEMON_ARGS)
fi

case "$action" in
  start|ensure)
    if pid="$(daemon_pid)"; then
      echo "[ros2_bridge] daemon already running (pid $pid)"
      exit 0
    fi
    echo "[ros2_bridge] starting daemon${extra:+ ${extra[*]}}"
    # `setsid` gives the daemon a session of its own, so it outlives the shell
    # that started it. Its stdout/stderr are sent to /dev/null here because the
    # daemon reopens both onto $ROS2_BRIDGE_LOG / $ROS2_BRIDGE_ERR itself
    # (--detach) - and holding the caller's pipe open until the daemon exits is
    # exactly what the Windows side's `start /b` exists to avoid:
    # `ros2_bridge.sh start | more` would hang until the bridge is stopped.
    setsid "$DAEMON" "${extra[@]}" --detach >/dev/null 2>&1 &
    child=$!
    pid=""
    for _ in $(seq 1 30); do
      if pid="$(daemon_pid)"; then
        break
      fi
      # It died before it ever got going (an option it did not like, a port
      # already in use): stop waiting and say where to look.
      if ! kill -0 "$child" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if [ -z "$pid" ]; then
      echo "[ros2_bridge] the daemon did not come up; run it in the foreground to see why:" >&2
      echo "[ros2_bridge]     bash bridge/linux/ros2_bridge.sh run" >&2
      exit 1
    fi
    echo "$pid" >"$PIDFILE"
    echo "[ros2_bridge] started pid $pid, log: $ROS2_BRIDGE_LOG"
    ;;

  run)
    echo "[ros2_bridge] running daemon in the foreground${extra:+ ${extra[*]}}"
    exec "$DAEMON" "${extra[@]}"
    ;;

  status)
    if pid="$(daemon_pid)"; then
      echo "[ros2_bridge] daemon running (pid $pid), log: $ROS2_BRIDGE_LOG"
    else
      echo "[ros2_bridge] daemon not running, log: $ROS2_BRIDGE_LOG"
    fi
    "$CLI" bridge status
    ;;

  pid)
    # For wait_bridge_ready.sh, which wants the answer and not the table.
    daemon_pid
    ;;

  stop)
    if ! pid="$(daemon_pid)"; then
      echo "[ros2_bridge] daemon not running"
      rm -f "$PIDFILE"
      exit 0
    fi
    echo "[ros2_bridge] stopping daemon (pid $pid)"
    kill "$pid" 2>/dev/null || true
    # `kill` answers before the process is really gone. A caller that lists
    # topics right afterwards - `ros2_bridge.sh stop` followed by
    # `ros2 topic list` is exactly what acceptance_check.py does on Windows -
    # would still find the dying daemon in the graph and read nothing. Wait for
    # it to disappear; the loop is a tenth of a second per step and normally
    # exits on the first check.
    for _ in $(seq 1 200); do
      if ! kill -0 "$pid" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
      echo "[ros2_bridge] warning: pid $pid did not exit after 20 s, killing it" >&2
      kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$PIDFILE"
    # Nothing is mirroring the agent any more, so the ready file must not claim
    # otherwise to a shell that starts the bridge again later.
    if [ -n "${ROS2_BRIDGE_READY_FILE:-}" ]; then
      rm -f "$ROS2_BRIDGE_READY_FILE"
    fi
    ;;

  log)
    tail -n "${1:-40}" "$ROS2_BRIDGE_LOG"
    ;;

  *)
    echo "usage: ros2_bridge.sh {start|run|status|pid|stop|log} [extra daemon options]" >&2
    exit 2
    ;;
esac
