#!/usr/bin/env python3
"""The two nav2 servers that are not part of the driving chain, beside a stack.

    python bridge/scripts/run_nav2_docking_route.py                 start both, stay up
    python bridge/scripts/run_nav2_docking_route.py --duration 60   exit after a minute
    python bridge/scripts/run_nav2_docking_route.py stop            stop both

``run_nav2_stack.py`` runs the chain a robot needs to be driven to a goal. These
two are upstream nav2 packages that this port compiles and links, but that
nothing in the chain depends on, so they are started on their own here - the same
arrangement ``run_nav2_safety.py`` uses for the velocity smoother and the
collision monitor. Neither is in the stack's ``lifecycle_manager.node_names`` and
the stack's own behaviour is left exactly as it was.

    docking_server   DockRobot / UndockRobot, on /docking_server
    route_server     ComputeRoute / ComputeAndTrackRoute, on /route_server

Neither has a caller in this repository yet, and neither ships the file it would
need to do real work (a dock description, a route graph). What this script
establishes is what has to hold before a caller is worth writing: both nodes
reach ACTIVE, and every plugin they instantiate while configuring -
``opennav_docking::SimpleChargingDock``, and the route planner with its edge
scorers and operations - is present in the port's plugin database. A plugin the
port cannot find is a configure failure, so "active" is the whole test.

Both are lifecycle nodes and neither is driven by the stack's lifecycle_manager,
so this script configures and activates them itself, over the same
``/<node>/change_state`` service the manager would use.

Logs: one file per node under ``$NAV2_DOCKING_ROUTE_LOG_DIR``
(``<temp>/nav2_docking_route``).

    ros2_bridge.bat start      (Windows)   /   bash bridge/linux/ros2_bridge.sh start
    python bridge/scripts/run_nav2_stack.py                     (in another window)

``bridge/windows/nav2_docking_route.bat`` is the cmd twin of this script and does
the same job in shell; the two are meant to stay in step.
"""
from __future__ import annotations

import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _bridge_platform as bp

TAG = "nav2_docking_route"
USAGE = """usage: run_nav2_docking_route.py [--duration S] [--params FILE]
       run_nav2_docking_route.py stop

  Starts docking_server and route_server next to a running nav2 stack and
  activates them. Needs the bridge up for /clock and /tf.

  --params FILE replaces nav2_shim_docking_route.yaml (the upstream params and
  the shim's overrides are still loaded first, in that order).

Environment: NAV2_DOCKING_ROUTE_LOG_DIR (node logs, default <temp>/nav2_docking_route).
"""


class Usage(Exception):
    """Bad command line, reported the way argparse would report it."""


def parse_options(argv):
    """`--duration S` and `--params FILE`; anything else is a mistake."""
    duration, config = "0", bp.DOCKING_ROUTE_YAML
    index = 0
    while index < len(argv):
        arg = argv[index].lower()
        if arg in ("--duration", "--params"):
            if index + 1 >= len(argv):
                raise Usage("%s needs a value" % argv[index])
            if arg == "--duration":
                duration = argv[index + 1]
            else:
                config = Path(argv[index + 1])
            index += 2
        else:
            raise Usage("unknown option '%s'" % argv[index])
    return duration, config


def main(argv) -> int:
    # `stop` is handled before the environment is touched: stopping must work
    # from a shell whose environment was never set up, and it needs nothing
    # from it.
    if argv and argv[0].lower() == "stop":
        was = bp.kill_nodes(bp.DOCKING_ROUTE_NODES,
                            note="[%s] stopping the previous servers" % TAG)
        print("[%s] %s" % (TAG, "stopped" if was else "nothing was running"), flush=True)
        return 0
    if argv and argv[0].lower() in ("--help", "-h"):
        print(USAGE)
        return 0

    try:
        duration, config = parse_options(argv)
    except Usage as bad:
        print("run_nav2_docking_route.py: %s" % bad, file=sys.stderr)
        print(USAGE, file=sys.stderr)
        return 2
    try:
        wait_s = float(duration)
    except ValueError:
        print("run_nav2_docking_route.py: --duration wants seconds, got '%s'" % duration,
              file=sys.stderr)
        return 2

    # A missing executable is worth naming here rather than three log lines
    # later: `cmake --build build -- docking_server route_server` is what builds
    # just these two.
    missing = bp.missing_exes(bp.DOCKING_ROUTE_NODES)
    if missing:
        for path in missing:
            print("[%s] %s is missing - build it first" % (TAG, path), file=sys.stderr)
        print("[%s] %s" % (TAG, bp.build_hint()), file=sys.stderr)
        return 1
    for path in (bp.STACK_PARAMS_YAML, bp.OVERRIDES_YAML, config):
        if not path.is_file():
            print("[%s] parameter file not found: %s" % (TAG, path), file=sys.stderr)
            return 1

    env = bp.environment()
    log_dir = Path(env.get("NAV2_DOCKING_ROUTE_LOG_DIR")
                   or (Path(tempfile.gettempdir()) / "nav2_docking_route"))

    # Both nodes stamp and look up transforms, so /clock and /tf have to be in
    # shared memory before they activate - and the bridge is what puts them there.
    if bp.wait_bridge_ready(env) != 0:
        return 1

    log_dir.mkdir(parents=True, exist_ok=True)
    for old in log_dir.glob("*.log"):
        try:
            old.unlink()
        except OSError:
            pass

    params_args = ["--params-file", str(bp.STACK_PARAMS_YAML),
                   "--params-file", str(bp.OVERRIDES_YAML),
                   "--params-file", str(config)]
    duration_args = [] if wait_s == 0 else ["--duration", "%g" % wait_s]

    # A previous run's nodes would hold the same lifecycle service names, and the
    # activation below would then talk to whichever answered first.
    bp.kill_nodes(bp.DOCKING_ROUTE_NODES)

    print("[%s] logs: %s" % (TAG, log_dir), flush=True)
    for name in bp.DOCKING_ROUTE_NODES:
        _, log_path = bp.spawn(bp.exe(name), params_args + duration_args,
                               env, log_dir, name)
        print("[%s]   %s -> %s" % (TAG, name, log_path), flush=True)

    if not bp.wait_for_change_state(bp.DOCKING_ROUTE_NODES, env):
        print("[%s] the two nodes did not come up; see %s" % (TAG, log_dir), file=sys.stderr)
        for name in bp.DOCKING_ROUTE_NODES:
            print("[%s]   %s" % (TAG, log_dir / (name + ".log")), file=sys.stderr)
        return 1
    if bp.configure_and_activate(bp.DOCKING_ROUTE_NODES, env, log_dir, TAG) != 0:
        return 1

    print("[%s] docking_server:  DockRobot / UndockRobot on /docking_server" % TAG, flush=True)
    print("[%s] route_server:    ComputeRoute / ComputeAndTrackRoute on /route_server" % TAG, flush=True)
    print("[%s] stop them with:  %s bridge/scripts/run_nav2_docking_route.py stop"
          % (TAG, bp.PYTHON), flush=True)

    # A `--duration` run waits here and then takes the nodes down with it; the
    # default leaves them running, because the servers live in the two background
    # processes and there is nothing left for this window to do.
    if wait_s == 0:
        return 0
    time.sleep(wait_s)
    bp.kill_nodes(bp.DOCKING_ROUTE_NODES, note="[%s] stopping the servers" % TAG)
    print("[%s] stopped" % TAG, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
