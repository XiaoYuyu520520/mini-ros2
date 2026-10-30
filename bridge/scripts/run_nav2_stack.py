#!/usr/bin/env python3
"""Bring the whole nav2 stack up, on Windows and on the Ubuntu robot alike.

    python bridge/scripts/run_nav2_stack.py                     stack only, stays up
    python bridge/scripts/run_nav2_stack.py --goal auto         then drive to a goal
    python bridge/scripts/run_nav2_stack.py --goal 3.5,-2 --report g.json --timeout 120
    python bridge/scripts/run_nav2_stack.py --duration 120      exit after two minutes
    python bridge/scripts/run_nav2_stack.py --no-amcl           no localization
    python bridge/scripts/run_nav2_stack.py --map map/ai.yaml   localize against a
                                                                saved map file
    python bridge/scripts/run_nav2_stack.py --safety            smooth and check the
                                                                commands on the way
                                                                to the wheels
    python bridge/scripts/run_nav2_stack.py --safety --safety-params F.yaml
    python bridge/scripts/run_nav2_stack.py stop

The environment ``bridge_env.bat`` / ``bridge_env.sh`` sets lives in this
process tree only - it is built here (``_bridge_platform.environment``) and
disappears with the process, so nothing about the machine changes.

What it starts, and why each piece is needed
--------------------------------------------
    amcl_bridge_node   map -> odom, from /scan and /map
    planner_server     global planning (NavFn), publishes /plan
    controller_server  path following (RPP), publishes /cmd_vel
    bt_navigator       the behavior tree that sequences the two above
    behavior_server    spin / wait / back-up, for the tree's recovery branch
    smoother_server    path smoothing over the SmoothPath action
    waypoint_follower  one NavigateToPose per pose, over FollowWaypoints
    lifecycle_manager  drives configure/activate on all of them

The order matters twice over. The servers come up *unconfigured* - upstream
nav2 expects a launch file to hand them to a lifecycle manager - so a manager is
what activates them, and it is also what makes ``nav2_goal`` usable at all: the
BT navigator only accepts a goal once it is active. AMCL goes first because both
costmaps resolve ``map -> base_link``, and that path only exists once AMCL is
publishing the ``map -> odom`` leg.

``behavior_server`` is what the recovery leaves of both behavior trees send
their goals to. It is not optional: a tree is built when bt_navigator activates,
and a BT action node whose server is missing makes the whole activation fail -
"Action server spin not available".

What it needs in shared memory, which the bridge daemon mirrors in from the
robot: ``/scan``, ``/odom``, ``/map``, ``/tf``, ``/tf_static`` and ``/clock``.
Start it with ``bridge\\windows\\ros2_bridge.bat start`` on Windows or
``bash bridge/linux/ros2_bridge.sh start`` here - or let any ``ros2`` command
start it for you.

``--no-amcl`` is for a machine that already has a ``map -> odom`` transform of
its own - ``run_slam_demo.py`` running in another window is the usual reason.
Without one, ``nav2_goal`` fails on its first transform lookup.

``--map FILE`` is the other source of that transform: the file - a PGM with its
YAML sidecar - is loaded by ``map_server``, which this script starts and
activates before AMCL, because AMCL is what turns a saved map into
``map -> odom``. It is the shim's twin of ``Run_nav.launch.py map:=FILE`` on the
car, and it needs the daemon *not* to be mirroring ``/map``: two writers on one
topic are two maps in one channel, the same reason a mapping run wants the mirror
off. The script refuses to start when the daemon is mirroring ``/map`` and prints
the restart command, rather than navigating against a map nobody chose.

``--safety`` puts the two nodes upstream runs *after* the controller into the
chain itself, so a velocity command reaches the wheels through the acceleration
limits in ``nav2_shim_safety.yaml`` and past the stop polygon the collision
monitor keeps in front of the robot (0.45 m, half a metre wide, against
``/scan``)::

    controller_server   -> /cmd_vel_nav       -r cmd_vel:=cmd_vel_nav
    behavior_server     -> /cmd_vel_nav       -r cmd_vel:=cmd_vel_nav
    velocity_smoother   -> /cmd_vel_smoothed  -r cmd_vel:=cmd_vel_nav
    collision_monitor   -> /cmd_vel           cmd_vel_out_topic

That is the shape of upstream's ``navigation_launch.py``. The two extra nodes
are not in the lifecycle manager's ``node_names``, so this script starts them and
drives their own ``/<node>/change_state`` services once the manager has reported
the rest of the stack active.

Logs of the background nodes are one file per node under
``$NAV2_STACK_LOG_DIR`` (``<temp>/nav2_stack``), because five processes writing
into one console is unreadable, and because the startup check reads the
lifecycle manager's log to know when the stack is actually up rather than
sleeping for a fixed moment.

``bridge/windows/nav2_stack.bat`` is the cmd twin of this script and does the
same job in shell; the two are meant to stay in step.
"""
from __future__ import annotations

import os
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _bridge_platform as bp

STACK_NODES = ["amcl_bridge_node", "planner_server", "controller_server", "bt_navigator",
               "behavior_server", "smoother_server", "waypoint_follower",
               "lifecycle_manager"]
SERVER_NODES = ["planner_server", "controller_server", "bt_navigator", "behavior_server",
                "smoother_server", "waypoint_follower"]
# --map only. map_server is a lifecycle node like the servers and, like the two
# --safety nodes, it is not in the manager's `node_names` - so this script drives
# its configure/activate pair itself.
MAP_NODES = ["map_server"]
# What the daemon has to mirror when map_server owns /map: everything the stack
# reads, minus the map it brings itself.
MIRROR_WITHOUT_MAP = ["/clock", "/odom", "/scan", "/tf", "/tf_static"]
SAFETY_NODES = bp.SAFETY_NODES
# What `stop` takes down, in the order the cmd twin uses: the manager first, so
# nothing is being reconfigured while its servers disappear.
STOP_NAMES = ["lifecycle_manager", "bt_navigator", "controller_server", "planner_server",
              "behavior_server", "smoother_server", "waypoint_follower",
              "amcl_bridge_node", "map_server", "velocity_smoother", "collision_monitor"]

# Remapped to /cmd_vel_nav under --safety: a node's topic names are fixed when
# it is built, so this is the only moment the move can happen.
CMD_VEL_WRITERS = ("controller_server", "behavior_server")

USAGE = """usage: run_nav2_stack.py [--safety] [--no-amcl] [--map FILE] [--duration S]
                           [--params FILE] [--no-overrides] [goal options]
       run_nav2_stack.py stop

  --params FILE replaces the upstream nav2_params.yaml the stack is built
  around. --no-overrides loads that file *instead of* nav2_shim_overrides.yaml,
  which is what a params file that already speaks the shim needs: the overrides
  carry use_sim_time, enable_stamped_cmd_vel, the behavior-tree paths and the
  lifecycle_manager block, and a file that carries them too would otherwise see
  every one of them restated. Use both together only when the extra file is
  meant to win - `racecar/src/racecar/config/nav.yaml` is the worked example.

  --map FILE loads that file - a PGM and its YAML sidecar - with map_server and
  makes it the stack's /map, the way `Run_nav.launch.py map:=FILE` does on the
  car. map_server is started and activated here, before AMCL, because AMCL is
  what turns the map into map -> odom. /map has one writer, so the daemon must
  not be mirroring the peer's map while this runs: the script refuses to start
  when it is, and prints the two commands that take /map off the mirror set.
  --map and --no-amcl together are a mistake - a saved map is what AMCL
  localises against - and are passed through as given.

  Any nav2_goal option - --goal X,Y|auto, --timeout S, --report FILE, --yaw,
  --frame - is passed to nav2_goal once the stack is active, e.g.
      run_nav2_stack.py --goal auto --report goal.json

  --safety puts velocity_smoother and collision_monitor in the command chain
  instead of beside it: controller_server and behavior_server are remapped to
  /cmd_vel_nav, the smoother publishes /cmd_vel_smoothed and the monitor is the
  only writer of /cmd_vel, the topic the robot drives from. Limits and the stop
  polygon are in nav2_shim_safety.yaml.

  --safety-params FILE loads one more node parameter file, last, for the two of
  them - which is how the polygon can be widened for a test run without editing
  nav2_shim_safety.yaml. It implies --safety.

Environment: NAV2_STACK_LOG_DIR (node logs, default <temp>/nav2_stack),
NAV2_STACK_WAIT_MS (budget for "Managed nodes are active", default 90000).
"""


class Usage(Exception):
    """Bad command line, reported the way argparse would report it."""


def parse_options(argv):
    """The cmd twin's option loop, over a Python argument list.

    Anything this script does not know is a goal command: it is run against the
    stack once the stack is active, and nav2_goal owns those options. That also
    means ``--goal 3.5,-2`` works here without the quotes cmd demands - nothing
    splits an argument on its comma.
    """
    amcl, safety, duration = True, False, "0"
    params = safety_params = map_file = None
    overrides = True
    goal = []
    index = 0
    while index < len(argv):
        arg = argv[index]
        low = arg.lower()
        if low == "--no-amcl":
            amcl = False
            index += 1
        elif low == "--safety":
            safety = True
            index += 1
        elif low == "--no-overrides":
            # The caller's params file is the whole configuration: it already
            # carries the four things nav2_shim_overrides.yaml exists to add.
            overrides = False
            index += 1
        elif low in ("--safety-params", "--duration", "--params", "--map"):
            if index + 1 >= len(argv):
                raise Usage("%s needs a value" % arg)
            value = argv[index + 1]
            if low == "--safety-params":
                # Loaded *after* nav2_shim_safety_chain.yaml, so it can restate
                # anything in either safety file without editing the repository
                # copies. It implies --safety.
                safety, safety_params = True, value
            elif low == "--duration":
                duration = value
            elif low == "--params":
                params = value
            else:
                map_file = value
            index += 2
        else:
            goal.append(arg)
            index += 1
    return amcl, safety, duration, params, safety_params, overrides, map_file, goal


def check_files(params_yaml, safety, safety_params, map_file):
    """Refuse to start half a stack, before anything is killed or started."""
    missing = bp.missing_exes(STACK_NODES)
    if missing:
        for path in missing:
            print("[nav2_stack] %s not found" % path, file=sys.stderr)
        print("[nav2_stack] %s" % bp.build_hint(), file=sys.stderr)
        return False
    if not params_yaml.is_file():
        print("[nav2_stack] parameter file not found: %s" % params_yaml, file=sys.stderr)
        return False
    if map_file:
        missing = bp.missing_exes(MAP_NODES)
        if missing:
            for path in missing:
                print("[nav2_stack] %s not found - it is what --map loads with" % path,
                      file=sys.stderr)
            print("[nav2_stack] %s" % bp.build_hint(), file=sys.stderr)
            return False
        if not Path(map_file).is_file():
            print("[nav2_stack] map file not found: %s" % map_file, file=sys.stderr)
            return False
    if not safety:
        return True
    # --safety adds two more nodes to the command path, so both binaries and
    # both parameter files have to be there before the stack is started at all:
    # a missing smoother would otherwise be discovered when the wheels do not
    # turn.
    missing = bp.missing_exes(SAFETY_NODES)
    if missing:
        for path in missing:
            print("[nav2_stack] %s not found - it is part of --safety" % path, file=sys.stderr)
        print("[nav2_stack] %s" % bp.build_hint(), file=sys.stderr)
        return False
    wanted = [bp.SAFETY_YAML, bp.SAFETY_CHAIN_YAML]
    if safety_params:
        wanted.append(Path(safety_params))
    for path in wanted:
        if not path.is_file():
            print("[nav2_stack] parameter file not found: %s" % path, file=sys.stderr)
            return False
    return True


def wait_for_active(log_dir: Path, budget_ms: int) -> int:
    """Poll the manager's log until it says the stack is active.

    0 = active, 1 = the bringup gave up, 2 = the budget ran out.
    """
    log = log_dir / "lifecycle_manager.log"
    steps = max(1, (budget_ms + 999) // 1000)
    for _ in range(steps):
        text = bp.tail(log, 4000)
        if "Managed nodes are active" in text:
            return 0
        # A bringup that has already given up is worth reporting now instead of
        # after the whole budget: the manager says which node failed, and that
        # node's own log says why - a costmap that never saw its transform,
        # most often.
        if "Aborting bringup" in text or "Failed to change state for node:" in text:
            return 1
        time.sleep(1.0)
    return 2


def start_safety(env, log_dir, params_args, duration_args, safety_params) -> int:
    """Start the two in-chain nodes and activate them."""
    args = list(params_args) + ["--params-file", str(bp.SAFETY_YAML),
                                "--params-file", str(bp.SAFETY_CHAIN_YAML)]
    if safety_params:
        args += ["--params-file", str(safety_params)]
    print("[nav2_stack] --safety: starting velocity_smoother and collision_monitor", flush=True)
    # velocity_smoother is the node that needs the remap: its input topic is a
    # literal `cmd_vel` in velocity_smoother.cpp, so `-r cmd_vel:=cmd_vel_nav` is
    # what makes it read the controller's stream instead of the empty name.
    # collision_monitor takes both topics from parameters, so it is started with
    # no remap at all.
    for name in SAFETY_NODES:
        extra = ["-r", "cmd_vel:=cmd_vel_nav"] if name == "velocity_smoother" else []
        _, log_path = bp.spawn(bp.exe(name), args + list(duration_args) + extra,
                               env, log_dir, name)
        print("[nav2_stack]   %s -> %s" % (name, log_path), flush=True)

    if not bp.wait_for_change_state(SAFETY_NODES, env):
        print("[nav2_stack] the two --safety nodes did not come up", file=sys.stderr)
        print("[nav2_stack] see %s and %s" % (log_dir / "velocity_smoother.log",
                                               log_dir / "collision_monitor.log"),
              file=sys.stderr)
        print("[nav2_stack] the rest of the stack is up; stop it with:  %s" % stop_hint(),
              file=sys.stderr)
        return 1
    if bp.configure_and_activate(SAFETY_NODES, env, log_dir, "nav2_stack") != 0:
        return 1
    print("[nav2_stack] command chain:  controller_server -> /cmd_vel_nav", flush=True)
    print("[nav2_stack]                  velocity_smoother -> /cmd_vel_smoothed", flush=True)
    print("[nav2_stack]                  collision_monitor -> /cmd_vel  (only writer)", flush=True)
    return 0


def unmirror_hint() -> str:
    """The two commands that take /map off the daemon's mirror set."""
    if bp.IS_WINDOWS:
        script = "bridge\\windows\\ros2_bridge.bat"
    else:
        script = "bash bridge/linux/ros2_bridge.sh"
    start = " ".join([script, "start"] + ["--mirror %s" % t for t in MIRROR_WITHOUT_MAP])
    return "%s stop   then   %s" % (script, start)


def mirrored_topics(env: dict) -> set:
    """The topics the daemon is mirroring in from the peer, by name.

    ``ros2 bridge status`` lists every topic in the shim graph with where it came
    from - ``[gRPC bridge]`` for the ones the daemon mirrors - so this is one
    line of its output per topic, read back. Nothing here decides anything on a
    topic that is merely *known*: only a mirrored one has a writer behind it.
    """
    done = bp.ros2(["bridge", "status"], env, capture=True)
    names = set()
    for line in (done.stdout or "").splitlines():
        fields = line.split()
        if fields and fields[0].startswith("/") and "gRPC bridge" in line:
            names.add(fields[0])
    return names


def start_map_server(env, log_dir, params_args, duration_args, map_file) -> int:
    """Load *map_file* and make it the stack's /map. 0 when it is active.

    The parameter files are named before ``-p yaml_filename`` on purpose: that
    is the order the loader applies them in, and a params file with a
    ``map_server`` block of its own - nav.yaml carries ``yaml_filename: ''`` -
    would otherwise win over the file the caller asked for.
    """
    path = Path(map_file).resolve()
    print("[nav2_stack] starting map_server  (%s)" % path, flush=True)
    _, log_path = bp.spawn(bp.exe("map_server"),
                           params_args + duration_args
                           + ["-p", "yaml_filename:=%s" % path],
                           env, log_dir, "map_server")
    print("[nav2_stack]   map_server -> %s" % log_path, flush=True)
    if not bp.wait_for_change_state(MAP_NODES, env):
        print("[nav2_stack] map_server did not come up", file=sys.stderr)
        print("[nav2_stack] see %s" % log_path, file=sys.stderr)
        print("[nav2_stack] the rest of the stack is not up either; there is nothing to"
              " stop", file=sys.stderr)
        return 1
    if bp.configure_and_activate(MAP_NODES, env, log_dir, "nav2_stack") != 0:
        return 1
    print("[nav2_stack] map_server -> /map  (it is the only writer of it)", flush=True)
    return 0


def stop_hint() -> str:
    return "%s bridge/scripts/run_nav2_stack.py stop" % bp.PYTHON


def main(argv) -> int:
    # `stop` is handled before the environment is touched: stopping must work
    # from a shell whose environment was never set up, and it needs nothing
    # from it.
    if argv and argv[0].lower() == "stop":
        was = bp.kill_nodes(STOP_NAMES, note="[nav2_stack] stopping the previous stack")
        if was:
            print("[nav2_stack] stopped", flush=True)
        else:
            print("[nav2_stack] nothing was running", flush=True)
        return 0
    if argv and argv[0].lower() in ("--help", "-h"):
        print(USAGE)
        return 0

    try:
        (amcl, safety, duration, params, safety_params, overrides, map_file,
         goal_args) = parse_options(argv)
    except Usage as bad:
        print("run_nav2_stack.py: %s" % bad, file=sys.stderr)
        print(USAGE, file=sys.stderr)
        return 2

    params_yaml = Path(params) if params else bp.STACK_PARAMS_YAML
    if not check_files(params_yaml, safety, safety_params, map_file):
        return 1

    env = bp.environment()
    log_dir = Path(env.get("NAV2_STACK_LOG_DIR")
                   or (Path(tempfile.gettempdir()) / "nav2_stack"))
    try:
        wait_ms = int(env.get("NAV2_STACK_WAIT_MS") or 90000)
    except ValueError:
        wait_ms = 90000

    # A stack left over from an earlier run would keep its topics and its
    # lifecycle state, and the new manager would then talk to two nodes of the
    # same name. Start from nothing.
    bp.kill_nodes(STOP_NAMES)

    # /scan, /map, /tf and /clock have to be in shared memory before the
    # servers activate, so start the daemon when it is down and wait for its
    # first sync.
    if bp.wait_bridge_ready(env) != 0:
        return 1

    log_dir.mkdir(parents=True, exist_ok=True)
    bp.clear_logs(log_dir)
    print("[nav2_stack] logs: %s" % log_dir, flush=True)

    # The overrides go last when they are wanted, so a `-p` naming something
    # both files set stays theirs - which is the point of a file that exists to
    # adapt an upstream one. `--no-overrides` drops them entirely.
    params_args = ["--params-file", str(params_yaml)]
    if overrides:
        params_args += ["--params-file", str(bp.OVERRIDES_YAML)]
    duration_args = [] if duration in ("0", "") else ["--duration", str(duration)]

    if map_file:
        # /map has one writer. The peer's map mirrored in and map_server's file
        # are two maps in one channel, and the one the costmaps localise against
        # is whichever wrote last - so a run that brings its own map needs the
        # mirror off, exactly like a mapping run. Refusing here is before
        # anything has been started, which is the only cheap moment to refuse.
        if "/map" in mirrored_topics(env):
            print("[nav2_stack] the daemon is mirroring /map from the peer, and --map"
                  " makes map_server its writer", file=sys.stderr)
            print("[nav2_stack] two writers on one topic are two maps in one channel;"
                  " restart the daemon without it:", file=sys.stderr)
            print("[nav2_stack]   %s" % unmirror_hint(), file=sys.stderr)
            return 2
        if start_map_server(env, log_dir, params_args, duration_args, map_file) != 0:
            return 1

    if amcl:
        print("[nav2_stack] starting amcl_bridge_node  (map -> odom)", flush=True)
        bp.spawn(bp.exe("amcl_bridge_node"),
                 params_args + duration_args
                 + ["-p", "base_frame_id:=base_link", "-p", "laser_max_range:=12.0",
                    "-p", "save_pose_rate:=0.0", "-p", "first_map_only:=true",
                    "--initial-pose-from-odom"],
                 env, log_dir, "amcl_bridge_node")

    remap = ["-r", "cmd_vel:=cmd_vel_nav"] if safety else []
    print("[nav2_stack] starting %s" % ", ".join(SERVER_NODES), flush=True)
    for name in SERVER_NODES:
        extra = remap if (safety and name in CMD_VEL_WRITERS) else []
        _, log_path = bp.spawn(bp.exe(name), params_args + duration_args + extra,
                               env, log_dir, name)
        print("[nav2_stack]   %s -> %s" % (name, log_path), flush=True)

    # The manager polls for each node's services, so the servers need a moment
    # to announce themselves; a cold start of four 20 Hz servers plus two
    # costmaps is the slowest thing here and it is not worth racing.
    print("[nav2_stack] starting lifecycle_manager", flush=True)
    time.sleep(4.0)
    _, manager_log = bp.spawn(bp.exe("lifecycle_manager"),
                              params_args + duration_args,
                              env, log_dir, "lifecycle_manager")

    status = wait_for_active(log_dir, wait_ms)
    if status == 1:
        print("[nav2_stack] the lifecycle manager could not bring the stack up", file=sys.stderr)
        for line in bp.tail(manager_log, 4000).splitlines():
            if "Failed" in line or "ERROR" in line:
                print(line, file=sys.stderr)
        print("[nav2_stack] node logs: %s" % log_dir, file=sys.stderr)
        print("[nav2_stack] stop the leftovers with:  %s" % stop_hint(), file=sys.stderr)
        return 1
    if status == 2:
        print("[nav2_stack] warning: the stack did not report itself active within %d ms"
              % wait_ms, file=sys.stderr)
        print("[nav2_stack] see %s" % manager_log, file=sys.stderr)
    else:
        print("[nav2_stack] %s are active" % ", ".join(SERVER_NODES), flush=True)

    # The two --safety nodes go up last, on purpose: the manager has activated
    # the controller and the behavior server by now, so nothing is going to
    # publish on /cmd_vel_nav - or read /cmd_vel - that is not part of the
    # chain, and the smoother's first input is a command the finished stack
    # produced.
    if safety and start_safety(env, log_dir, params_args, duration_args,
                              safety_params) != 0:
        return 1

    # A goal sent in the first second after activation can be lost: the shim's
    # action channels are per-process shared-memory rings, and a request written
    # while a server is still attaching to its channels is seen by nobody. What
    # that looks like is not an error - bt_navigator accepts the goal, the tree
    # ticks, and no /cmd_vel ever comes out, because the compute_path_to_pose
    # request the tree sent was never answered, so the goal runs until the
    # caller's timeout. Four seconds of patience is cheap next to a 25 s bringup.
    time.sleep(4.0)

    if not goal_args:
        print("[nav2_stack] the stack is running; stop it with:  %s" % stop_hint(), flush=True)
        return 0

    # The goal runs in the foreground: it prints progress, and its JSON report
    # says whether the robot actually moved - which is the thing worth checking,
    # not the exit code.
    code = bp.run_foreground(bp.exe("nav2_goal"), goal_args, env)
    if duration in ("0", ""):
        print("[nav2_stack] the stack is still running; stop it with:  %s" % stop_hint(),
              flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
