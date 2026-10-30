#!/usr/bin/env python3
"""Save whatever is on a map topic to PGM/PNG plus its YAML - on either platform.

    python bridge/scripts/save_map.py -f racecar/map/ai_map001
    python bridge/scripts/save_map.py -f C:/maps/hall --fmt png --topic /slam_map
    python bridge/scripts/save_map.py            # writes <temp>/map.pgm + .yaml

This is the `ros2 run nav2_map_server map_saver_cli` of this side of the bridge:
the port ships `map_saver_cli`, which subscribes to one topic, writes one pair of
files and exits. Nothing here sets the environment either -
``_bridge_platform.environment`` builds the variables ``bridge_env.bat`` /
``bridge_env.sh`` export, and they disappear with this process.

Which map it saves is decided by what is publishing:

    /map        the simulator's or the robot's map, mirrored by the daemon (the
                usual case), or ``map_server`` reading a file
    /slam_map   ``run_slam_demo.py`` (slam_toolbox), when the daemon is not
                mirroring a map of its own - which is what a mapping run wants,
                because two writers on /map are two maps in one channel

``--fmt`` fixes the file names: ``pgm`` writes BASE.pgm + BASE.yaml, ``png``
writes BASE.png + BASE.yaml. ``--mode scale`` wants ``--fmt png``: a PGM has no
alpha channel, and the scaled map carries its free space in one.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _bridge_platform as bp

MAP_SAVER = bp.SHIM_BUILD / ("map_saver_cli" + (".exe" if bp.IS_WINDOWS else ""))

USAGE = """usage: save_map.py [-f BASE] [--topic /map] [--fmt pgm|png] [--mode MODE]
                   [--occ T] [--free T] [map_saver_cli options...]

  Writes BASE.pgm (or BASE.png) and BASE.yaml next to each other; the default
  BASE is <temp>/map. Anything this script does not recognise is handed to
  map_saver_cli as it stands.
"""


def parse_options(argv):
    """`-f` / `--topic` / `--fmt`, and the rest passes through to map_saver_cli."""
    base = Path(tempfile.gettempdir()) / "map"
    topic, fmt = "/map", "pgm"
    extra = []
    index = 0
    while index < len(argv):
        arg = argv[index]
        low = arg.lower()
        if low in ("-f", "--file", "-t", "--topic", "--fmt", "--mode", "--occ", "--free"):
            if index + 1 >= len(argv):
                raise ValueError("%s needs a value" % arg)
            value = argv[index + 1]
            if low in ("-f", "--file"):
                base = Path(value)
            elif low in ("-t", "--topic"):
                topic = value
            elif low == "--fmt":
                fmt = value
            else:
                extra += [arg, value]
            index += 2
        elif arg.startswith("-f") and len(arg) > 2:
            base = Path(arg[2:])
            index += 1
        elif arg.startswith("-t") and len(arg) > 2:
            topic = arg[2:]
            index += 1
        elif arg.startswith("-"):
            extra.append(arg)
            index += 1
        else:
            base = Path(arg)
            index += 1
    return base, topic, fmt, extra


def main(argv) -> int:
    if argv and argv[0].lower() in ("--help", "-h"):
        print(USAGE)
        return 0

    try:
        base, topic, fmt, extra = parse_options(argv)
    except ValueError as bad:
        print("save_map.py: %s" % bad, file=sys.stderr)
        print(USAGE, file=sys.stderr)
        return 2

    if not MAP_SAVER.is_file():
        print("[save_map] %s not found" % MAP_SAVER, file=sys.stderr)
        print("[save_map] %s" % bp.build_hint(), file=sys.stderr)
        return 1

    env = bp.environment()
    # The daemon has to be up and past its first sync, or what gets saved is the
    # empty map that never arrived.
    if bp.wait_bridge_ready(env) != 0:
        return 1

    print("[save_map] %s -> %s.%s + %s.yaml" % (topic, base, fmt, base), flush=True)
    return bp.run_foreground(MAP_SAVER,
                             ["-t", topic, "-f", str(base), "--fmt", fmt] + extra,
                             env)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
