#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# bridge_env.sh - environment for the ROS2 shim + gRPC bridge on Linux.
#
#     source bridge/linux/bridge_env.sh
#
# The Linux half of bridge\windows\bridge_env.bat: the same variables, the same
# values, the same promise - every one of them lives in the shell that sourced
# this file and disappears with it. It has to be *sourced*: run as a script it
# would set them in a child process that exits immediately.
#
# Override anything by setting it beforehand:
#     ROS2_BRIDGE_AGENT=192.168.1.113:50051 source bridge/linux/bridge_env.sh
#     ROS2_BRIDGE_AGENT=192.168.1.113:50051 bash bridge/linux/ros2.sh topic list
# ---------------------------------------------------------------------------

if [ -z "${BASH_VERSION:-}" ]; then
  echo "bridge_env.sh needs bash: it resolves its own path through BASH_SOURCE." >&2
  echo "Use 'bash bridge/linux/ros2.sh ...' from a POSIX shell." >&2
  return 1 2>/dev/null || exit 1
fi

# Resolved from this file, so `source` works from any working directory.
ROS2_BRIDGE_LINUX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROS2_BRIDGE_CPU_ROOT="$(cd "$ROS2_BRIDGE_LINUX/../.." && pwd)"

# The build tree is called `bridge/windows/build` on *both* platforms - the name
# is historical (bridge/scripts/build_bridge.py's DEFAULT_BUILD) and splitting it
# per platform would mean two spellings of every path in every document. What
# does change per platform is the binary's name: there is no `.exe` here.
: "${ROS2_BRIDGE_BUILD:=$ROS2_BRIDGE_CPU_ROOT/bridge/windows/build}"

# Shared-memory geometry. Publisher and subscriber have to agree, so exporting
# the same values into every process of this shell is what makes the shim
# channels line up - the daemon and the `ros2` CLI included.
#
# The values are the connector's, not shm_config.hpp's defaults: once the relay
# merges a real ROS2 graph in, the daemon mirrors ~80 topics at once and the
# geometry is multiplied by all of them (2 MB/8 MB -> ~2.4 GB, 512 KB/2 MB ->
# ~600 MB). The largest measured message is a 528 KB global costmap, so 2 MB
# still has headroom.
: "${NAV2_SHM_SLOT_COUNT:=8}"
: "${NAV2_SHM_SLOT_SIZE:=524288}"
: "${NAV2_SHM_LARGE_SLOT_SIZE:=2097152}"

# Where the ported nav2 packages are. NAV2_SHIM_SRC answers the ament shim's
# $(find-pkg-share <pkg>) lookup: the port builds the extracted sources in place
# instead of installing a share tree, so nav2_bt_navigator's behavior_trees are
# at <NAV2_SHIM_SRC>/nav2_bt_navigator/behavior_trees.
: "${NAV2_SHIM_SRC:=$ROS2_BRIDGE_CPU_ROOT/nav2/nav2_extracted/navigation2-1.5.1}"
: "${NAV2_SHIM_BT_DIR:=$ROS2_BRIDGE_CPU_ROOT/nav2/ros2_shim/config/bt}"

# The graph every process in this shell publishes into. Left at the shim's own
# default (`temp_directory_path()/nav2_shim_graph`, see
# nav2/ros2_shim/include/ros2_shim/graph.hpp) so that this file and a process
# which never saw it agree by construction.
: "${NAV2_SHM_GRAPH_DIR:=${TMPDIR:-/tmp}/nav2_shim_graph}"
: "${ROS2_SHM_GRAPH_DIR:=$NAV2_SHM_GRAPH_DIR}"

# Log level of the shim's rclcpp-compatible logger: debug | info | warn | error
# | fatal. `info` hides the DEBUG chatter of the ported nav2 code; set
# NAV2_SHM_LOG_LEVEL=debug before sourcing this to get it back.
: "${NAV2_SHM_LOG_LEVEL:=info}"

# Discovery / connection defaults for the gRPC SDK client.
: "${ROS2_GRPC_SDK_CONFIG_DIR:=$ROS2_BRIDGE_CPU_ROOT/gRPC/ros2_gRPC_sdk/config}"

# `ros2.sh` and the demo scripts start the daemon on demand unless this is 0.
: "${ROS2_BRIDGE_AUTOSTART:=1}"

# Waiting for the daemon: it creates ROS2_BRIDGE_READY_FILE as soon as its first
# connection attempt has settled, and wait_bridge_ready.sh polls for that file
# rather than sleeping and hoping the LAN scan finished. A cold start needs the
# scan plus the first topic sync, so the budget is in whole seconds.
: "${ROS2_BRIDGE_READY_FILE:=$ROS2_BRIDGE_BUILD/ros2_bridge_daemon.ready}"
: "${ROS2_BRIDGE_WAIT_MS:=30000}"

: "${ROS2_BRIDGE_LOG:=$ROS2_BRIDGE_BUILD/ros2_bridge_daemon.log}"
: "${ROS2_BRIDGE_ERR:=$ROS2_BRIDGE_BUILD/ros2_bridge_daemon.err.log}"
# A detached daemon appends to those two files for its whole life, so past this
# many bytes it renames a log to <path>.1 and starts a fresh one
# (bridge/windows/src/bridge_daemon.cpp). 0 turns the check off.
: "${ROS2_BRIDGE_LOG_MAX:=16777216}"

# Put the shim's CLI on PATH, so that in this shell `ros2 topic list -t` works
# with no path at all - the Linux answer to what ros2.bat gives a cmd window.
# Prepended, and only here: this file is the one thing that claims the name
# `ros2`, and a real ROS2 installation (there is none on the robot) has to be
# given the shell first - `PATH=/opt/ros/humble/bin:$PATH source bridge/linux/bridge_env.sh`.
case ":$PATH:" in
  *":$ROS2_BRIDGE_BUILD:"*) ;;
  *) PATH="$ROS2_BRIDGE_BUILD:$PATH" ;;
esac
export PATH

export ROS2_BRIDGE_LINUX ROS2_BRIDGE_CPU_ROOT ROS2_BRIDGE_BUILD \
       NAV2_SHM_SLOT_COUNT NAV2_SHM_SLOT_SIZE NAV2_SHM_LARGE_SLOT_SIZE \
       NAV2_SHIM_SRC NAV2_SHIM_BT_DIR NAV2_SHM_GRAPH_DIR ROS2_SHM_GRAPH_DIR \
       NAV2_SHM_LOG_LEVEL ROS2_GRPC_SDK_CONFIG_DIR ROS2_BRIDGE_AUTOSTART \
       ROS2_BRIDGE_READY_FILE ROS2_BRIDGE_WAIT_MS ROS2_BRIDGE_LOG \
       ROS2_BRIDGE_ERR ROS2_BRIDGE_LOG_MAX
