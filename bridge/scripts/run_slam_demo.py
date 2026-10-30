#!/usr/bin/env python3
"""Run slam_toolbox on this side of the bridge - Windows or the Ubuntu robot.

    python bridge/scripts/run_slam_demo.py                            /scan -> /map
    python bridge/scripts/run_slam_demo.py -p map_name:=/slam_map --duration 60
    python bridge/scripts/run_slam_demo.py --mode async

The environment ``bridge_env.bat`` / ``bridge_env.sh`` sets lives in this
process tree only - it is built here (``_bridge_platform.environment``) and
disappears with the process, so nothing about the machine changes.

What it needs in shared memory is ``/scan`` and ``/tf``, which the bridge daemon
mirrors in from the robot::

    python bridge/scripts/ros2_bridge.py start        (or any ros2 command)

The node publishes its map on ``/map`` by default, and that is the same topic
the daemon mirrors the robot's map onto. When SLAM is meant to be the map
source, restart the daemon without ``/map`` first::

    ros2_bridge.bat stop
    ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan ^
        --mirror /tf --mirror /tf_static

Upstream's own ``*_node`` binaries only spin - a lifecycle manager drives
configure/activate in a normal ROS2 deployment and there is none on this side of
the bridge. ``slam_bridge_node`` does both transitions itself.

Parameters
----------
slam_toolbox defaults to the ``base_footprint`` robot frame; the bundled
simulator publishes ``base_link``, so ``-p base_frame:=base_link`` is preset.
The preset steps aside as soon as the caller says anything about the robot frame
themselves - ``base_frame`` anywhere in the arguments, or a ``--params-file``
that brings its own. Other parameters (``map_name``, ``map_update_interval``,
...) do not get in the way of it: they are the common case, and dropping the
preset for them used to leave the node looking for a ``base_footprint`` frame
that this simulator never publishes.

``NAV2_SLAM_PARAMS`` is the explicit override and always applies;
``NAV2_SLAM_EXE`` picks a different binary (the port also builds
``async_slam_toolbox_node`` and ``localization_slam_toolbox_node``).

``bridge/windows/slam_demo.bat`` is the cmd twin of this script and does the
same job in shell; the two are meant to stay in step.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _bridge_platform as bp

SLAM_EXE = bp.SHIM_BUILD / "slam_toolbox_port" / ("slam_bridge_node" + (".exe" if bp.IS_WINDOWS else ""))
DEFAULT_PARAMS = "-p base_frame:=base_link"

USAGE = """usage: run_slam_demo.py [node options]

  Every option is handed to slam_bridge_node; the ones worth knowing are the
  node's own -p NAME:=VALUE, --params-file FILE, --mode async|sync and
  --duration S. It runs in the foreground: Ctrl+C (or --duration) ends it.

  -p base_frame:=base_link is preset, and withdrawn when the arguments carry
  base_frame themselves or bring a --params-file; NAV2_SLAM_PARAMS overrides
  the preset outright, NAV2_SLAM_EXE the binary.
"""


def main(argv) -> int:
    if argv and argv[0].lower() in ("--help", "-h"):
        print(USAGE)
        return 0

    target = Path(os.environ.get("NAV2_SLAM_EXE") or SLAM_EXE)
    if not target.is_file():
        print("[slam_demo] %s not found" % target, file=sys.stderr)
        print("[slam_demo] %s" % bp.build_hint(), file=sys.stderr)
        return 1

    env = bp.environment()
    args = bp.preset_params(argv, "NAV2_SLAM_PARAMS", DEFAULT_PARAMS).split() + list(argv)

    # /scan and /tf have to be in shared memory before the node activates, so
    # start the daemon when it is down and wait for its first topic sync.
    if bp.wait_bridge_ready(env) != 0:
        return 1
    return bp.run_foreground(target, args, env)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
