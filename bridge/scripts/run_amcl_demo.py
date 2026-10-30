#!/usr/bin/env python3
"""Localize with nav2_amcl on this side of the bridge - Windows or the Ubuntu robot.

    python bridge/scripts/run_amcl_demo.py                          /odom seeds the pose
    python bridge/scripts/run_amcl_demo.py --initial-pose 1.5,-2,0  x,y,yaw in radians
    python bridge/scripts/run_amcl_demo.py -p laser_max_range:=12.0 --duration 120

The environment ``bridge_env.bat`` / ``bridge_env.sh`` sets lives in this
process tree only - it is built here (``_bridge_platform.environment``) and
disappears with the process, so nothing about the machine changes.

What it needs in shared memory is ``/scan``, ``/map`` and ``/tf``, which the
bridge daemon mirrors in from the robot::

    ros2_bridge.bat start      (Windows)   /   bash bridge/linux/ros2_bridge.sh start

AMCL publishes the corrected pose on ``/amcl_pose`` and the ``map`` -> ``odom``
transform on ``/tf``. Unless ``--initial-pose`` says otherwise it starts from the
pose on ``/odom``, so a robot that is switched on where its odometry puts it
needs no arguments at all.

Upstream's own ``amcl`` binary only spins - a lifecycle manager drives
configure/activate in a normal ROS2 deployment and there is none on this side of
the bridge. ``amcl_bridge_node`` does both transitions itself.

How well it tracks is worth checking rather than assuming::

    nav2/ros2_shim/build/amcl_probe --duration 60

compares ``/amcl_pose`` against ``/odom`` both at the newest sample and at the
instant AMCL stamped its estimate, which is what separates a wrong estimate from
a stale one.

Parameters
----------
nav2_amcl defaults to the ``base_footprint`` robot frame while this simulator
publishes ``base_link``, and its default ``laser_max_range`` of 100 m turns a
beam that saw nothing into a plausible 100 m return - the simulated lidar
reports ``range_max`` 12 m, so the preset matches it.

The presets step aside when the caller says something about them: any argument
mentioning the robot frame (``base_frame``), or a ``--params-file`` that brings
its own. Parameters for everything else - ``laser_max_range``,
``save_pose_rate``, ``max_beams``, ... - leave them alone, which is what keeps a
plain ``-p max_beams:=60`` from costing the caller the frame preset.
``NAV2_AMCL_PARAMS`` is the explicit override and always applies,
``NAV2_AMCL_EXE`` picks a different binary.

``bridge/windows/amcl_demo.bat`` is the cmd twin of this script and does the same
job in shell; the two are meant to stay in step.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _bridge_platform as bp

DEFAULT_PARAMS = ("-p base_frame_id:=base_link -p laser_max_range:=12.0 "
                  "-p save_pose_rate:=0.0 -p first_map_only:=true")

USAGE = """usage: run_amcl_demo.py [node options]

  Every option is handed to amcl_bridge_node; the node's own are -p NAME:=VALUE,
  --params-file FILE, --initial-pose X,Y,YAW and --duration S. It runs in the
  foreground: Ctrl+C (or --duration) ends it.

  -p base_frame_id:=base_link -p laser_max_range:=12.0 -p save_pose_rate:=0.0
  -p first_map_only:=true is preset, and withdrawn when the arguments mention
  base_frame or bring a --params-file; NAV2_AMCL_PARAMS overrides the preset
  outright, NAV2_AMCL_EXE the binary.

  Without --initial-pose the node takes its starting pose from /odom.
"""


def main(argv) -> int:
    if argv and argv[0].lower() in ("--help", "-h"):
        print(USAGE)
        return 0

    target = Path(os.environ.get("NAV2_AMCL_EXE")
                  or bp.exe("amcl_bridge_node"))
    if not target.is_file():
        print("[amcl_demo] %s not found" % target, file=sys.stderr)
        print("[amcl_demo] %s" % bp.build_hint(), file=sys.stderr)
        return 1

    env = bp.environment()
    args = bp.preset_params(argv, "NAV2_AMCL_PARAMS", DEFAULT_PARAMS).split()
    # Start from /odom unless the caller supplies a pose of their own.
    if not any(a.lower() == "--initial-pose" for a in argv):
        args.append("--initial-pose-from-odom")
    args += list(argv)

    # /scan, /map and /tf have to be in shared memory before AMCL activates, so
    # start the daemon when it is down and wait for its first topic sync.
    if bp.wait_bridge_ready(env) != 0:
        return 1
    return bp.run_foreground(target, args, env)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
