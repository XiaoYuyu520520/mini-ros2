#!/usr/bin/env python3
"""The two nodes between the controller and the wheels, beside a running stack.

    python bridge/scripts/run_nav2_safety.py                 start both, stay up
    python bridge/scripts/run_nav2_safety.py --duration 60   exit after a minute
    python bridge/scripts/run_nav2_safety.py stop            stop both

``run_nav2_stack.py`` runs the planning and driving chain: ``controller_server``
writes ``/cmd_vel`` and the robot takes its orders from there. The two nodes here
are the ones upstream puts *after* the controller in a ``nav2_bringup`` launch
file, and they are the reason this is a separate script: neither is in the
stack's ``lifecycle_manager.node_names``, and the stack's chain is left exactly
as it was.

    velocity_smoother   /cmd_vel          -> /cmd_vel_smoothed
    collision_monitor   /cmd_vel_smoothed -> /cmd_vel_monitored

What that buys, with no remapping anywhere: the same velocity stream the robot is
driving from is also run through the acceleration limits in
``nav2_shim_safety.yaml``, and then checked against ``/scan`` with a stop polygon
0.45 m ahead of the robot. ``/cmd_vel_smoothed`` is what the controller's
commands look like once they are inside the platform's acceleration envelope;
``/cmd_vel_monitored`` is what they look like once the laser has had a say.
Neither topic has another writer, so both can be run against a live stack without
touching what the robot does.

Watching is not the only way to run the pair. ``run_nav2_stack.py --safety``
starts these same two nodes *in* the chain instead: the controller and the
behavior server are remapped to ``/cmd_vel_nav``, the smoother follows them there
- nav2_velocity_smoother's topics are hardcoded in its own source (``cmd_vel``
in, ``cmd_vel_smoothed`` out), which is why upstream remaps them from its launch
file and why that mode is a flag on the stack script - and the monitor's
``cmd_vel_out_topic`` goes back to upstream's ``cmd_vel``. See
``nav2_shim_safety_chain.yaml``, which is that one parameter.

Use this script for looking and that mode for driving: run at once, they would
put a second reader on ``/cmd_vel_smoothed`` and a second writer on the
``/cmd_vel`` the monitor owns.

Both nodes are lifecycle nodes and neither is driven by the stack's
lifecycle_manager, so this script configures and activates them itself, over the
same ``/<node>/change_state`` service the manager would use.

Logs: one file per node under ``$NAV2_SAFETY_LOG_DIR`` (``<temp>/nav2_safety``).

    ros2_bridge.bat start      (Windows)   /   bash bridge/linux/ros2_bridge.sh start

``nav2_stack.bat --safety`` runs these same two nodes in-chain;
``bridge/windows/nav2_safety.bat`` is the cmd twin of this script and does the
same job in shell. The three are meant to stay in step.
"""
from __future__ import annotations

import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _bridge_platform as bp

TAG = "nav2_safety"
USAGE = """usage: run_nav2_safety.py [--duration S] [--params FILE]
       run_nav2_safety.py stop

  Starts velocity_smoother and collision_monitor next to a running nav2 stack and
  activates them. Needs the bridge up for /scan, /odom and /tf.

  --params FILE replaces nav2_shim_safety.yaml (the upstream params and the
  shim's overrides are still loaded first, in that order).

Environment: NAV2_SAFETY_LOG_DIR (node logs, default <temp>/nav2_safety).
"""


class Usage(Exception):
    """Bad command line, reported the way argparse would report it."""


def parse_options(argv):
    """`--duration S` and `--params FILE`; anything else is a mistake."""
    duration, config = "0", bp.SAFETY_YAML
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
        was = bp.kill_nodes(bp.SAFETY_NODES,
                            note="[%s] stopping the previous safety nodes" % TAG)
        print("[%s] %s" % (TAG, "stopped" if was else "nothing was running"), flush=True)
        return 0
    if argv and argv[0].lower() in ("--help", "-h"):
        print(USAGE)
        return 0

    try:
        duration, config = parse_options(argv)
    except Usage as bad:
        print("run_nav2_safety.py: %s" % bad, file=sys.stderr)
        print(USAGE, file=sys.stderr)
        return 2
    try:
        wait_s = float(duration)
    except ValueError:
        print("run_nav2_safety.py: --duration wants seconds, got '%s'" % duration,
              file=sys.stderr)
        return 2

    missing = bp.missing_exes(bp.SAFETY_NODES)
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
    log_dir = Path(env.get("NAV2_SAFETY_LOG_DIR")
                   or (Path(tempfile.gettempdir()) / "nav2_safety"))

    # /scan and /tf have to be in shared memory before either node activates -
    # the monitor reads the laser and looks up `base_link` in its frame.
    if bp.wait_bridge_ready(env) != 0:
        return 1

    log_dir.mkdir(parents=True, exist_ok=True)
    for old in log_dir.glob("*.log"):
        try:
            old.unlink()
        except OSError:
            pass

    # The two safety files are named last because that is the order the loader
    # applies them in: nav2_shim_safety_chain.yaml is what moves the monitor's
    # output, and a value from nav2_params.yaml would win if it were named after.
    params_args = ["--params-file", str(bp.STACK_PARAMS_YAML),
                   "--params-file", str(bp.OVERRIDES_YAML),
                   "--params-file", str(config)]
    duration_args = [] if wait_s == 0 else ["--duration", "%g" % wait_s]

    # A previous run's nodes would hold the same topic names and the same
    # lifecycle services; the graph would then have two nodes called
    # `velocity_smoother` and the activation below would talk to whichever
    # answered first.
    bp.kill_nodes(bp.SAFETY_NODES)

    print("[%s] logs: %s" % (TAG, log_dir), flush=True)
    for name in bp.SAFETY_NODES:
        _, log_path = bp.spawn(bp.exe(name), params_args + duration_args,
                               env, log_dir, name)
        print("[%s]   %s -> %s" % (TAG, name, log_path), flush=True)

    if not bp.wait_for_change_state(bp.SAFETY_NODES, env):
        print("[%s] the two nodes did not come up; see %s" % (TAG, log_dir), file=sys.stderr)
        for name in bp.SAFETY_NODES:
            print("[%s]   %s" % (TAG, log_dir / (name + ".log")), file=sys.stderr)
        return 1
    if bp.configure_and_activate(bp.SAFETY_NODES, env, log_dir, TAG) != 0:
        return 1

    print("[%s] velocity_smoother:  /cmd_vel          -> /cmd_vel_smoothed" % TAG, flush=True)
    print("[%s] collision_monitor:  /cmd_vel_smoothed -> /cmd_vel_monitored" % TAG, flush=True)
    print("[%s] watch them with:" % TAG, flush=True)
    print("[%s]   ros2 topic echo /cmd_vel_smoothed" % TAG, flush=True)
    print("[%s]   ros2 topic echo /cmd_vel_monitored" % TAG, flush=True)
    print("[%s] stop them with:  %s bridge/scripts/run_nav2_safety.py stop"
          % (TAG, bp.PYTHON), flush=True)

    # A `--duration` run waits here and then takes the nodes down with it; the
    # default leaves them running, because the control loop lives in the two
    # background processes and there is nothing left for this window to do.
    if wait_s == 0:
        return 0
    time.sleep(wait_s)
    bp.kill_nodes(bp.SAFETY_NODES, note="[%s] stopping the safety nodes" % TAG)
    print("[%s] stopped" % TAG, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
