#!/usr/bin/env python3
"""End-to-end acceptance check for the gRPC bridge + shim + nav2 demo.

This script walks the four acceptance criteria of the project and reports
PASS/FAIL for each of them, so a validation run is one command instead of a
handful of manual steps:

1. *Windows sees the Ubuntu topics* - the daemon is killed first, then
   ``ros2.bat topic list -t`` has to come back on its own (fast LAN scan,
   persistent gRPC channel, mirror into shared memory) with the expected
   topics and their types.
2. *Ubuntu sees the Windows topics* - ``ros2 topic pub /cmd_vel ...`` is
   published on Windows, and the change has to show up in the ``/odom`` that
   the Ubuntu simulator publishes back. A second check makes the first one
   mean something: a command that *stops* arriving has to stop the robot
   (``cmd_vel_watchdog``), because the simulator used to latch the last twist
   and keep driving - which made "the car moved" true for a bridge that had
   already died. A third check closes the other half of that direction: nav2
   *answers* on ``/plan`` and ``/behavior_tree_log``, and those two only reach
   the robot because the daemon's default uplink table names them
   (``uplink_nav2``).
3. *nav2 plans and drives* - ``nav2_bridge_demo`` builds a costmap from the
   bridged ``/map`` + ``/scan``, plans with the ported ``nav2_navfn_planner``
   and drives the car to the goal over ``/cmd_vel``.
4. *SLAM maps a lap* - the daemon is restarted without ``--mirror /map``
   so ``slam_demo.bat`` can own that topic, the car drives four
   "2 m + 90 deg" legs, and the resulting OccupancyGrid has to have
   walls in it. This is the only check that changes the daemon's mirror
   set, and it puts the default daemon back before it returns.

The demo drives the car away from the origin, so a fifth check puts it back at
the end: ``amcl_bridge_node`` seeds AMCL's initial pose from ``/odom`` when the
stack starts, and a car parked elsewhere would make the next run localise itself
wrong and reject every goal with "Start Coordinates was outside bounds". That
check used to be housekeeping whose exit status was thrown away, which is how
the field-less-request defect behind it stayed invisible; it asserts the
response *and* the resulting ``/odom`` now (evidence 59).

Every check uses the same temporary environment as ``bridge_env.bat``; nothing
is persisted except the report. Each check works in a directory of its own
under ``%TEMP%`` and removes it before returning: the earlier version wrote
straight into ``%TEMP%`` and left every map, pose graph and log of every run
behind (52 entries / 58 MB had accumulated by 2026-09-17,
bridge/docs/05-audit.md section 3.4). A check that fails keeps its directory
and names it in the report instead - the node log in there is the only record
of what went wrong.

Examples
--------
    python bridge/scripts/acceptance_check.py
    python bridge/scripts/acceptance_check.py --quick          # criteria 1 and 2 only
    python bridge/scripts/acceptance_check.py --skip-slam      # criteria 1, 2 and 3
    python bridge/scripts/acceptance_check.py --report out.json
    python bridge/scripts/acceptance_check.py --keep-daemon    # do not restart the bridge
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WINDOWS_ROOT = REPO_ROOT / "bridge" / "windows"
ROS2_BAT = WINDOWS_ROOT / "ros2.bat"
DAEMON_BAT = WINDOWS_ROOT / "ros2_bridge.bat"
RUN_DEMO = REPO_ROOT / "bridge" / "scripts" / "run_nav2_demo.py"
DEFAULT_REPORT = REPO_ROOT / "bridge" / "docs" / "validation" / "acceptance-report.json"

# `map_dump.exe` and the SLAM node need the same shared-memory geometry
# the daemon uses, which `run_nav2_demo.py` already knows how to build.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_nav2_demo import DEFAULT_MSYS2, build_environment  # noqa: E402

# The Ubuntu simulator publishes exactly these topics; `ros2 topic list -t` has
# to report all of them with their ROS2 type names.
EXPECTED_TOPICS = {
    "/clock": "rosgraph_msgs/msg/Clock",
    "/cmd_vel": "geometry_msgs/msg/Twist",
    "/map": "nav_msgs/msg/OccupancyGrid",
    "/odom": "nav_msgs/msg/Odometry",
    "/scan": "sensor_msgs/msg/LaserScan",
    "/tf": "tf2_msgs/msg/TFMessage",
    "/tf_static": "tf2_msgs/msg/TFMessage",
}

# The daemon's default uplink table, mirrored from `Options::uplink` in
# `bridge/windows/src/bridge_daemon.cpp`: the topics a Windows publisher writes
# that are forwarded to the robot. Kept as a table here because nothing else in
# this script can see it - a daemon whose defaults were edited down still drives
# the car to its goal, so criterion 2 passes either way without a check that
# reads this list.
DEFAULT_UPLINKS = {
    "/cmd_vel": "geometry_msgs/msg/Twist",
    "/goal_pose": "geometry_msgs/msg/PoseStamped",
    "/initialpose": "geometry_msgs/msg/PoseWithCovarianceStamped",
    "/plan": "nav_msgs/msg/Path",
    "/behavior_tree_log": "nav2_msgs/msg/BehaviorTreeLog",
}

# The two nav2 answers on, and the half `uplink_nav2` asserts. `/plan` is the
# one that can be published from here; the behaviour-tree log is published by
# `bt_navigator`, so it is checked for presence and type only.
NAV2_UPLINKS = ("/plan", "/behavior_tree_log")

TOPIC_LINE = re.compile(r"^(\S+)\s+\[([^\]]+)\]\s*$")
POSITION = re.compile(r"pose\.pose\.position:\s*\{x:\s*([-\d.eE+]+),\s*y:\s*([-\d.eE+]+)")
LINEAR = re.compile(r"twist\.twist\.linear:\s*\{x:\s*([-\d.eE+]+)")
RATE = re.compile(r"average rate:\s*([\d.]+)")
# `std_srvs/srv/Trigger`'s response, as `ros2 service call` prints it.
RESPONSE_OK = re.compile(r"^success:\s*true\s*$", re.M)


class Result:
    def __init__(self, name, title):
        self.name = name
        self.title = title
        self.ok = False
        self.detail = ""
        self.seconds = 0.0
        self.data = {}

    def as_dict(self):
        return {
            "check": self.name,
            "title": self.title,
            "ok": self.ok,
            "seconds": round(self.seconds, 2),
            "detail": self.detail,
            "data": self.data,
        }


def say(message):
    print(message, flush=True)


def run(args, timeout=120.0):
    """Run a command through cmd.exe with the bridge environment in place."""
    return subprocess.run(
        ["cmd", "/c", *[str(a) for a in args]],
        cwd=str(REPO_ROOT), capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=timeout)


def ros2(*args, timeout=60.0):
    return run([ROS2_BAT, *args], timeout=timeout)


def daemon_running():
    out = subprocess.run(["tasklist", "/fi", "imagename eq ros2_bridge_daemon.exe"],
                         capture_output=True, text=True).stdout
    return "ros2_bridge_daemon.exe" in out


def odom_pose():
    """Latest /odom as (x, y, linear.x)."""
    proc = ros2("topic", "echo", "/odom", "--count", "1", timeout=30)
    if proc.returncode != 0:
        raise RuntimeError("topic echo /odom failed: " + proc.stderr.strip())
    pos = POSITION.search(proc.stdout)
    lin = LINEAR.search(proc.stdout)
    if not pos:
        raise RuntimeError("could not parse /odom:\n" + proc.stdout)
    return float(pos.group(1)), float(pos.group(2)), float(lin.group(1)) if lin else 0.0


# The daemon's status table (`--status-ms`, 5 s by default) is the only place
# the uplink counters are visible from outside: `ros2_bridge.bat status` prints
# the graph, not the counters. `ros2_bridge.bat status` does name the log file.
DAEMON_LOG_LINE = re.compile(r"^\s*\[ros2_bridge\][^\n]*\blog:\s*(\S.*?)\s*$", re.M)
UPLINK_COUNTER = re.compile(r"^\s*\^\s+(\S+)\s+(\S+)\s+msgs=(\d+)\s+failed=(\d+)\s*$", re.M)


def daemon_log():
    """The daemon's log file, as `ros2_bridge.bat status` reports it."""
    proc = run([DAEMON_BAT, "status"], timeout=90)
    match = DAEMON_LOG_LINE.search(proc.stdout or "")
    if not match:
        raise RuntimeError("`ros2_bridge.bat status` named no log file:\n"
                           + (proc.stdout or "")[-400:])
    return Path(match.group(1))


def uplink_counters(log_path, offset=0, tail=524288):
    """The uplink block of the daemon's status table, keyed by topic name.

    One line per *armed* uplink - `^ /plan nav_msgs/msg/Path msgs=16 failed=0`.
    The table is read from the log rather than from `ros2 topic list` because
    the log is where the per-uplink counters live; the graph only says the
    topic exists.

    `offset` is a byte offset into the log, and everything before it is
    ignored. The log is opened in append mode and survives daemon restarts
    (`rotate_log` only fires at 16 MiB), so without a floor the *previous*
    daemon's last block is what gets read - on 2026-09-17 that made this
    check fail with a counter that went backwards, 20 -> 17, because the old
    daemon's 20 was taken as this one's baseline.
    """
    with open(log_path, "rb") as handle:
        handle.seek(0, os.SEEK_END)
        end = handle.tell()
        handle.seek(max(offset, end - tail))
        text = handle.read().decode("utf-8", errors="replace")
    return {m.group(1): (m.group(2), int(m.group(3)), int(m.group(4)))
            for m in UPLINK_COUNTER.finditer(text)}


# --------------------------------------------------------------------- checks

def check_topic_list(result, cold):
    """Criterion 1: a Windows shell can list (and type) the Ubuntu topics."""
    started = time.monotonic()
    if cold:
        say("[bridge] stopping the daemon to force a cold start ...")
        run([DAEMON_BAT, "stop"], timeout=60)

    proc = ros2("topic", "list", "-t", timeout=120)
    result.seconds = time.monotonic() - started
    if proc.returncode != 0:
        result.detail = "ros2 topic list -t exited %d: %s" % (proc.returncode, proc.stderr.strip())
        return

    found = {}
    for line in proc.stdout.splitlines():
        match = TOPIC_LINE.match(line.strip())
        if match:
            found[match.group(1)] = match.group(2)

    missing = {t: v for t, v in EXPECTED_TOPICS.items() if t not in found}
    wrong = {t: (found[t], v) for t, v in EXPECTED_TOPICS.items()
             if t in found and found[t] != v}
    result.data = {
        "cold_start": cold,
        "seconds": round(result.seconds, 2),
        "topics": found,
        "missing": sorted(missing),
        "wrong_type": dict((t, list(v)) for t, v in wrong.items()),
    }
    if missing or wrong:
        result.detail = "missing %s; wrong type %s" % (sorted(missing), wrong)
        return
    result.ok = True
    result.detail = ("%d topics with types in %.1f s%s" %
                     (len(found), result.seconds, " (cold start)" if cold else ""))


def check_topic_info(result):
    """The /scan that Windows sees is reported as coming from the remote peer."""
    started = time.monotonic()
    proc = ros2("topic", "info", "/scan", timeout=30)
    result.seconds = time.monotonic() - started
    if proc.returncode != 0:
        result.detail = "topic info /scan exited %d" % proc.returncode
        return
    remote = "remote" in proc.stdout
    typed = "sensor_msgs/msg/LaserScan" in proc.stdout
    result.data = {"remote": remote, "type_ok": typed}
    result.ok = remote and typed
    result.detail = ("reported as remote (bridged over gRPC)" if result.ok
                     else proc.stdout.strip())


def check_scan_rate(result, seconds=6.0):
    """The mirrored /scan keeps the 10 Hz of the Ubuntu simulator."""
    started = time.monotonic()
    proc = subprocess.Popen(
        ["cmd", "/c", str(ROS2_BAT), "topic", "hz", "/scan", "--window", "20"],
        cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace")
    rates = []
    try:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            line = proc.stdout.readline()
            if not line:
                break
            match = RATE.search(line)
            if match:
                rates.append(float(match.group(1)))
    finally:
        # `proc` is the `cmd /c` wrapper, and killing it does not kill what it
        # started. That is not a theory: every run of this script used to leave
        # a live `ros2 topic hz /scan` behind - still holding a subscription on
        # the graph (it showed up as a second `/ros2_cli_sub` in `ros2 node
        # list`) and still holding `bridge\windows\build\ros2.exe` open, so
        # the next `build_bridge.py --target ros2` died with "cannot open
        # output file ...: Permission denied" for a reason that had nothing to
        # do with the change being built. `/T` is the whole tree: cmd, the bat
        # and the CLI.
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
            capture_output=True, text=True)
        proc.wait(timeout=10)
    result.seconds = time.monotonic() - started
    if not rates:
        result.detail = "no rate was reported by `ros2 topic hz /scan`"
        return
    average = sum(rates) / len(rates)
    result.data = {"samples": len(rates), "average_hz": round(average, 3),
                   "min_hz": round(min(rates), 3), "max_hz": round(max(rates), 3)}
    result.ok = 8.0 <= average <= 12.0
    result.detail = "%.2f Hz average over %d samples" % (average, len(rates))


def check_uplink(result):
    """Criterion 2: a Windows publisher moves the Ubuntu robot."""
    started = time.monotonic()
    before_x, before_y, _ = odom_pose()

    # The publisher runs in the background so that `/odom` can be read *while*
    # the command is still arriving, and it has to be read that way: `sim.py`
    # zeroes the twist half a second after the last /cmd_vel, so a reading
    # taken once `topic pub` has returned cannot ever see the commanded speed
    # - and one taken before the watchdog existed saw it for the wrong reason,
    # because the simulator latched the last command for ever.
    pub = subprocess.Popen(
        ["cmd", "/c", str(ROS2_BAT), "topic", "pub", "/cmd_vel",
         "geometry_msgs/msg/Twist", "linear.x=0.25", "angular.z=0.3",
         "--rate", "10", "--duration", "3"],
        cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace")
    try:
        time.sleep(1.5)                     # one and a half seconds into it
        during_linear = odom_pose()[2]
        pub.wait(timeout=90)
    finally:
        if pub.poll() is None:
            # `pub` is a `cmd /c` wrapper; /T is the whole tree, for the same
            # reason `check_scan_rate` kills its `topic hz` that way.
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(pub.pid)],
                           capture_output=True, text=True)
            pub.wait(timeout=10)

    # The watchdog stops the car ~0.5 s after the last message, so this is the
    # position it came to rest at, not the position the publisher left it in.
    time.sleep(0.6)
    after_x, after_y, _ = odom_pose()
    # Leave the car stopped, whatever the outcome.
    ros2("topic", "pub", "/cmd_vel", "geometry_msgs/msg/Twist",
         "linear.x=0.0", "angular.z=0.0", "--once", timeout=30)
    result.seconds = time.monotonic() - started

    moved = ((after_x - before_x) ** 2 + (after_y - before_y) ** 2) ** 0.5
    result.data = {
        "before": [round(before_x, 4), round(before_y, 4)],
        "after": [round(after_x, 4), round(after_y, 4)],
        "moved_m": round(moved, 4),
        "odom_twist_while_commanded": round(during_linear, 4),
    }
    # 0.25 m/s for ~3 s is ~0.75 m; anything below 0.15 m means the command did
    # not reach the simulator.
    result.ok = moved > 0.15 and abs(during_linear - 0.25) < 0.05
    result.detail = ("moved %.2f m, /odom twist.linear.x=%.3f while commanded"
                     % (moved, during_linear))
    if not result.ok:
        result.detail = "published /cmd_vel did not reach the simulator: " + result.detail


def check_uplink_nav2(result, seconds=30.0):
    """Criterion 2, the nav2 half: the plan nav2 produces reaches the robot.

    Everything else in this script points *at* the robot - `check_uplink`
    publishes /cmd_vel, `check_cmd_vel_watchdog` stops it, and the demo drives.
    nav2 also answers back, on `/plan` and `/behavior_tree_log`, and those two
    names are forwarded only because `bridge_daemon.cpp` puts them in the
    default uplink table. Nothing else here reads that table, so a daemon
    started without it (`--no-uplink`, or a default that was edited down) would
    still pass every check that came before this one: the car drives to the
    goal and the plan never leaves the machine.

    Two things are asserted:

    1. once something publishes each of the two nav2 names, the daemon arms
       that uplink: its status table grows a `^ <name> <type> msgs=...` line
       with the *documented* type. A name that is not in `Options::uplink` can
       never be armed, so this is the default table seen from outside.
    2. a second `/plan` publish moves that line's `msgs=` counter with
       `failed=0`, i.e. every message was converted and the robot accepted it.
       The counters are the ones the manual runs quote
       (bridge/docs/03-validation.md, evidence 54).

    The uplinks arm lazily - `startUplinks` re-scans every `--refresh-ms`
    (2 s) and needs a source for the type - so each name is published once as a
    warm-up and a second, identical `/plan` publish is what gets measured. That
    is also why `--once` is the wrong shape to measure with (04-gaps.md, 十四).

    What this cannot prove: that a *node* on the robot consumed the plan. The
    simulator only watches `/plan` when it is started with `--subscribe
    /plan:nav_msgs/msg/Path`, so the field-by-field decode stays an
    operator-side observation rather than something this check can require.
    """
    started = time.monotonic()
    log_path = daemon_log()
    # Everything this check reads has to be written after it started, or the
    # previous daemon's counters are read as this one's baseline. See
    # `uplink_counters`.
    offset = log_path.stat().st_size

    # Warm up: an uplink only arms once `startUplinks` (every `--refresh-ms`,
    # 2 s) can name the type, and the name comes from a *live* local publisher
    # - a one second publish can fall entirely inside a refresh window and
    # never be seen. Three seconds is one refresh window plus the margin for
    # the worker thread to open the channel and subscribe.
    warmup = {}
    for name in NAV2_UPLINKS:
        warmup[name] = ros2("topic", "pub", name, DEFAULT_UPLINKS[name],
                            "--rate", "10", "--duration", "3", timeout=90).returncode

    armed = {}
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        armed = uplink_counters(log_path, offset)
        if all(name in armed for name in NAV2_UPLINKS):
            break
        time.sleep(2.5)  # the daemon re-scans every 2 s and prints every 5 s

    unarmed = {name: (armed.get(name), DEFAULT_UPLINKS[name])
               for name in NAV2_UPLINKS
               if name not in armed or armed[name][0] != DEFAULT_UPLINKS[name]}
    counted = armed.get("/plan", (None, 0, 0))
    expected = 20  # `--rate 10 --duration 2`, the publish measured below

    result.seconds = time.monotonic() - started
    result.data = {
        "armed_table": {name: list(value) for name, value in sorted(armed.items())},
        "documented_table": dict(DEFAULT_UPLINKS),
        "warmup_exit": warmup,
        "plan_before": list(counted) if counted[0] else None,
        "plan_after": None,
        "publisher_exit": None,
    }
    # Nothing to measure if the table is not there - and the failure is named
    # by *which* names are missing, which is what the operator has to fix.
    if unarmed:
        result.detail = ("the daemon never armed %s, so the default uplink table "
                         "does not carry it (the table should be %s)"
                         % (unarmed, ", ".join(DEFAULT_UPLINKS)))
        return

    # The measured publish has the same shape as the warm-up, so the counter
    # now starts from an armed uplink and every message in it should land.
    pub = ros2("topic", "pub", "/plan", "nav_msgs/msg/Path", "header.frame_id=map",
               "--rate", "10", "--duration", "2", timeout=120)

    after = armed
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        time.sleep(2.5)
        after = uplink_counters(log_path, offset)
        # Half of them is enough to stop waiting: the status table is printed
        # on a 5 s timer, so the last few lines of it can always lag the
        # publish by up to that much.
        if after.get("/plan", (None, 0, 0))[1] - counted[1] >= expected // 2:
            break
    forwarded = after.get("/plan")
    result.seconds = time.monotonic() - started
    result.data["plan_after"] = list(forwarded) if forwarded else None
    result.data["publisher_exit"] = pub.returncode

    if forwarded is None:
        result.detail = "no `^ /plan ... msgs=` line after offset %d in %s" % (offset, log_path)
        return
    delta = forwarded[1] - counted[1]
    if delta < 5:
        result.detail = ("/plan moved the uplink counter by %d of %d messages "
                         "(%d -> %d); publisher exited %d, warm-ups exited %s"
                         % (delta, expected, counted[1], forwarded[1],
                            pub.returncode, warmup))
        return
    failed = {name: after[name][2] for name in NAV2_UPLINKS if after[name][2]}
    if failed:
        result.detail = ("the uplinks were armed but messages failed: %s (armed "
                         "table: %s)" % (failed, after))
        return
    result.ok = True
    result.detail = ("default table armed %s; /plan forwarded %d/%d msgs, failed=0"
                     % (", ".join(NAV2_UPLINKS), delta, expected))


def check_cmd_vel_watchdog(result, seconds=1.0, settle=1.5):
    """A /cmd_vel that stops arriving stops the robot.

    The audit's first finding (bridge/docs/05-audit.md section 6): the
    simulator latched the last command and integrated it for ever, so a bridge
    that died mid-command left the car driving away - and this file still said
    PASS, because "the car moved" was the only thing being measured. A real
    robot is bounded by `velocity_smoother`'s `cmd_vel_timeout` and the
    collision monitor; the simulator answers for it itself now (`sim.py`'s
    watchdog, 0.5 s by default).

    This check publishes a speed and *no* stop command after it. It then wants
    both halves: that the car did drive (otherwise a dead bridge would satisfy
    it), and that it is standing still, with `twist.linear.x == 0`, a second
    later.
    """
    started = time.monotonic()
    before_x, before_y, _ = odom_pose()

    pub = ros2("topic", "pub", "/cmd_vel", "geometry_msgs/msg/Twist",
               "linear.x=0.25", "angular.z=0.0", "--rate", "10",
               "--duration", str(seconds), timeout=60)
    if pub.returncode != 0:
        result.seconds = time.monotonic() - started
        result.detail = "topic pub failed: " + pub.stderr.strip()
        return

    time.sleep(settle)  # well past the 0.5 s watchdog in sim.py
    after_x, after_y, after_linear = odom_pose()
    time.sleep(1.0)
    later_x, later_y, later_linear = odom_pose()
    result.seconds = time.monotonic() - started

    drove = ((after_x - before_x) ** 2 + (after_y - before_y) ** 2) ** 0.5
    coasted = ((later_x - after_x) ** 2 + (later_y - after_y) ** 2) ** 0.5
    result.data = {
        "published_for_s": seconds,
        "drove_m": round(drove, 4),
        "coasted_m_after_1s": round(coasted, 4),
        "linear_x_settled": round(after_linear, 6),
        "linear_x_a_second_later": round(later_linear, 6),
    }
    # 0.25 m/s for 1 s is ~0.25 m less the watchdog's own 0.5 s of coast, so
    # anything over 0.10 m says the command reached the robot at all.
    result.ok = (drove > 0.10 and abs(after_linear) < 1e-6
                 and abs(later_linear) < 1e-6 and coasted < 0.02)
    result.detail = ("drove %.2f m on a %.0f s command, then %.3f m in the next second, "
                     "/odom twist.linear.x=%g" % (drove, seconds, coasted, later_linear))
    if drove <= 0.10:
        result.detail = "the command never reached the robot: " + result.detail
    elif coasted >= 0.02:
        result.detail = "the robot kept driving after the command stopped: " + result.detail


def check_nav2_demo(result, goal, timeout):
    """Criterion 3: nav2 plans a path and drives the car to the goal."""
    started = time.monotonic()
    # The demo's report is a file because that is how `run_nav2_demo.py` hands
    # its result over; it is not something to keep. `mkdtemp` + `rmtree` (and
    # not `%TEMP%\acceptance-nav2-<pid>.json`) is what stops a crashed run
    # from leaving one behind.
    workdir = Path(tempfile.mkdtemp(prefix="acceptance-nav2-"))
    report_path = workdir / "report.json"
    try:
        proc = run([sys.executable, RUN_DEMO, "--goal", goal, "--no-topics",
                    "--timeout", str(timeout), "--report", str(report_path)],
                   timeout=timeout + 180)
        result.seconds = time.monotonic() - started

        if not report_path.is_file():
            result.detail = "demo wrote no report (exit %d)" % proc.returncode
            result.data = {"exit_code": proc.returncode, "stdout_tail": proc.stdout[-800:]}
            return
        report = json.loads(report_path.read_text(encoding="utf-8"))
        result.data = report
        tolerance = 0.30
        result.ok = bool(report.get("reached")) and report.get("remaining_m", 1e9) <= tolerance
        result.detail = (
            "goal %s reached from %s: travelled %.2f m, remaining %.2f m, plan %s waypoints" % (
                report.get("goal"), report.get("start_pose", [])[:2],
                report.get("travelled_m", 0), report.get("remaining_m", -1),
                report.get("plan", {}).get("waypoints")))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


# ---------------------------------------------------------------- slamming

SLAM_DEMO = WINDOWS_ROOT / "slam_demo.bat"
SHIM_BUILD = REPO_ROOT / "nav2" / "ros2_shim" / "build"
MAP_DUMP = SHIM_BUILD / "map_dump.exe"
SLAM_NODE_EXE = "slam_bridge_node.exe"

# The daemon has to stop mirroring /map for slam_toolbox to be the publisher.
MIRRORS_WITHOUT_MAP = ["--mirror", "/clock", "--mirror", "/odom",
                       "--mirror", "/scan", "--mirror", "/tf",
                       "--mirror", "/tf_static"]

# `map_dump.cpp:189` prints the tally this parses.
MAPDUMP_LINE = re.compile(
    r"MAPDUMP frames=(\d+) frame=(\S+) size=(\d+)x(\d+) res=([\d.]+) "
    r"origin=\(([-\d.]+), ([-\d.]+)\) occupied=(\d+) free=(\d+) unknown=(\d+)")


def stop_daemon():
    run([DAEMON_BAT, "stop"], timeout=120)


def start_daemon(mirrors=None):
    run([DAEMON_BAT, "start", *(mirrors or [])], timeout=240)


def wait_for_topic(name, present=True, timeout=150.0):
    """Poll `ros2 topic list` until `name` appears (or disappears)."""
    deadline = time.monotonic() + timeout
    topics = set()
    while time.monotonic() < deadline:
        proc = ros2("topic", "list", timeout=60)
        topics = set((proc.stdout or "").split())
        if (name in topics) == present:
            return topics
        time.sleep(3)
    return topics


def map_dump(topic="/map", settle=6.0, ascii_width=56, out=None, timeout=240.0):
    """Read one OccupancyGrid through the shim and return the parsed tally.

    ``map_dump.exe`` has to share the shim's shared-memory geometry with the
    daemon, so it gets the same environment `run_nav2_demo.py` builds - the
    same one `bridge_env.bat` sets in a cmd session.
    """
    args = [str(MAP_DUMP), "--topic", topic, "--settle", str(settle),
            "--ascii", str(ascii_width)]
    if out is not None:
        args += ["--out", str(out)]
    proc = subprocess.run(args, cwd=str(REPO_ROOT), env=build_environment(DEFAULT_MSYS2),
                          capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)
    text = (proc.stdout or "") + (proc.stderr or "")
    match = MAPDUMP_LINE.search(text)
    if not match:
        raise RuntimeError("map_dump printed no MAPDUMP line:\n" + text[-800:])
    return {
        "returncode": proc.returncode,
        "frames": int(match.group(1)),
        "frame_id": match.group(2),
        "width": int(match.group(3)),
        "height": int(match.group(4)),
        "resolution": float(match.group(5)),
        "origin": [float(match.group(6)), float(match.group(7))],
        "occupied": int(match.group(8)),
        "free": int(match.group(9)),
        "unknown": int(match.group(10)),
    }


def check_slam(result, legs=4, seconds=600.0):
    """Criterion 4: slam_toolbox builds a map from the bridged /scan + /tf.

    The map is only interesting when the robot has moved, so the check drives
    the same closed lap the manual audit used - four "2 m forward + 90 deg"
    legs - and then requires the resulting OccupancyGrid to have real walls in
    it. An empty map is 100% unknown; the lap puts a few hundred cells on it.

    This is the only check that changes the daemon's mirror set, so it puts the
    default daemon back before returning.
    """
    started = time.monotonic()
    # One directory per run, and `%TEMP%` only keeps it when the check failed
    # (see the `finally` below). The earlier version wrote
    # `%TEMP%\acceptance-slam-<pid>.*` straight into `%TEMP%` and removed none
    # of it: 52 entries / 58 MB had accumulated by 2026-09-17
    # (bridge/docs/05-audit.md section 3.4).
    workdir = Path(tempfile.mkdtemp(prefix="acceptance-slam-"))
    header = workdir / "lap"
    slam_log = header.with_suffix(".log")
    pgm = header.with_suffix(".pgm")
    # The reload phase writes this lap's pose graph here and reads it back in a
    # second process. Both suffixes are `serialize_map`'s own, and both files
    # have to appear before there is anything to read back.
    archive = header.with_name(header.name + "-reload")
    reload_pgm = header.with_name(header.name + ".reload.pgm")
    reload_log = header.with_name(header.name + ".reload.log")
    slam = None
    log = None
    try:
        say("[slam] stopping the daemon and restarting it without --mirror /map ...")
        run(["taskkill", "/F", "/IM", SLAM_NODE_EXE], timeout=60)
        stop_daemon()
        start_daemon(MIRRORS_WITHOUT_MAP)

        topics = wait_for_topic("/map", present=False, timeout=150)
        if "/map" in topics:
            result.detail = "/map is still advertised after a daemon restart without it"
            return
        if "/scan" not in topics:
            result.detail = "the daemon never mirrored /scan: %s" % sorted(topics)
            return

        ros2("service", "call", "/sim/reset", "std_srvs/srv/Trigger", timeout=60)

        say("[slam] starting slam_demo.bat ...")
        log = open(slam_log, "w", encoding="utf-8")
        slam = subprocess.Popen(
            ["cmd", "/c", str(SLAM_DEMO), "-p", "base_frame:=base_link",
             "-p", "max_laser_range:=12.0", "-p", "map_update_interval:=2.0",
             "--duration", str(int(seconds))],
            cwd=str(REPO_ROOT), stdout=log, stderr=subprocess.STDOUT)

        deadline = time.monotonic() + 90
        publishing = False
        while time.monotonic() < deadline:
            info = ros2("topic", "info", "/map", timeout=60)
            if "Publisher count: 1" in info.stdout and "local" in info.stdout:
                publishing = True
                break
            time.sleep(4)
        if not publishing:
            result.detail = "slam_toolbox never advertised a local /map"
            return

        say("[slam] mapping a closed lap: %d x (2 m + 90 deg)" % legs)
        for leg in range(legs):
            drive = ros2("action", "send_goal", "/sim/drive",
                         "ros2_bridge_sim/action/Drive", "distance=2.0",
                         "--timeout", "45", timeout=180)
            spin = ros2("action", "send_goal", "/sim/spin",
                        "nav2_msgs/action/Spin", "target_yaw=1.5708",
                        "--timeout", "30", timeout=120)
            say("[slam]   leg %d/%d: drive exit %d, spin exit %d"
                % (leg + 1, legs, drive.returncode, spin.returncode))
        ros2("service", "call", "/sim/status", "std_srvs/srv/Trigger", timeout=60)
        time.sleep(6)

        tally = map_dump(topic="/map", settle=6.0, ascii_width=56, out=pgm)
        result.data = {"lap_legs": legs, "map": tally, "pgm": str(pgm),
                       "node_log": str(slam_log)}
        # The manual audit produced 372 - 483 occupied cells on this lap; an
        # empty or never-updated map is zero.
        mapped_ok = tally["occupied"] >= 200 and tally["width"] >= 50
        result.detail = ("%dx%d @ %.3f m, %d occupied / %d free / %d unknown"
                         % (tally["width"], tally["height"], tally["resolution"],
                            tally["occupied"], tally["free"], tally["unknown"]))

        # ------------------------------------------------------- reload it
        # A pose graph is only worth writing if a *later* process can pick it
        # up, and that is a different question from whether the file reads
        # back: the node that loads nothing still publishes /map - an empty
        # one - so the tally, not the /map stream, is the answer. This is the
        # `localization` flavour on the archive the mapping flavour just wrote
        # (bridge/docs/03-validation.md, evidence 47).
        say("[slam] serialize_map -> %s" % archive)
        ros2("service", "call", "/slam_toolbox/serialize_map",
             "slam_toolbox/srv/SerializePoseGraph", "filename=" + archive.as_posix(),
             timeout=180)
        time.sleep(1)
        written = {suffix: Path(str(archive) + suffix).exists()
                   for suffix in (".posegraph", ".data")}

        say("[slam] a second process, --mode localization, on that archive ...")
        run(["taskkill", "/F", "/IM", SLAM_NODE_EXE], timeout=60)
        try:
            slam.wait(timeout=30)
        except subprocess.TimeoutExpired:
            slam.kill()
        log.close()

        log = open(reload_log, "w", encoding="utf-8")
        slam = subprocess.Popen(
            ["cmd", "/c", str(SLAM_DEMO), "--mode", "localization",
             "-p", "base_frame:=base_link", "-p", "max_laser_range:=12.0",
             "-p", "map_file_name:=" + archive.as_posix(),
             # One of these two has to be given: without it the node starts
             # anyway, publishes an empty map, and the only sign is one ERROR
             # line in its log. `map_start_at_dock` is the one that needs no
             # pose from the caller.
             "-p", "map_start_at_dock:=true", "--duration", "180"],
            cwd=str(REPO_ROOT), stdout=log, stderr=subprocess.STDOUT)

        deadline = time.monotonic() + 90
        reloading = False
        while time.monotonic() < deadline:
            info = ros2("topic", "info", "/map", timeout=60)
            if "Publisher count: 1" in info.stdout and "local" in info.stdout:
                reloading = True
                break
            time.sleep(4)
        reloaded = None
        if reloading:
            time.sleep(8)
            reloaded = map_dump(topic="/map", settle=6.0, ascii_width=56, out=reload_pgm)

        result.data["reload"] = {
            "archive": str(archive), "files": written, "map": reloaded,
            "pgm": str(reload_pgm), "node_log": str(reload_log),
        }
        # The same archive, so the same map. Half of what the mapping process
        # had is already "this is not the map that was written"; an archive
        # that did not load leaves the node on an empty one.
        reload_ok = (all(written.values()) and reloaded is not None
                     and reloaded["occupied"] >= 200
                     and reloaded["occupied"] >= tally["occupied"] // 2
                     and reloaded["width"] >= 50)
        if reloaded is not None:
            result.detail += ("; reloaded in a fresh process: %dx%d, %d occupied"
                              % (reloaded["width"], reloaded["height"],
                                 reloaded["occupied"]))
        elif not reloading:
            result.detail += "; the localization node never advertised a local /map"

        result.ok = mapped_ok and reload_ok
        if not mapped_ok:
            result.detail = "the SLAM map never filled in: " + result.detail
        elif not reload_ok:
            result.detail = "the archive did not come back: " + result.detail
    finally:
        result.seconds = time.monotonic() - started
        if slam is not None:
            run(["taskkill", "/F", "/IM", SLAM_NODE_EXE], timeout=60)
            try:
                slam.wait(timeout=30)
            except subprocess.TimeoutExpired:
                slam.kill()
        if log is not None:
            log.close()
        say("[slam] restoring the default daemon ...")
        stop_daemon()
        start_daemon()
        wait_for_topic("/scan", present=True, timeout=150)
        # A passing run has nothing left to look at; a failing one keeps the
        # directory and names it, because the node log in there is the only
        # record of what went wrong.
        if result.ok:
            shutil.rmtree(workdir, ignore_errors=True)
        else:
            result.data["kept"] = str(workdir)
            say("[slam] kept %s for the node logs" % workdir)


# ----------------------------------------------------------------------- main

def check_reset(result):  # noqa: D401
    """Put the car back at the origin, and *prove* it happened.

    This used to run as housekeeping with its exit status thrown away - the only
    trace of a failure was a "[warn] /sim/reset exited 1" line nobody read. That
    is how a whole class of failure stayed invisible: for as long as the bridge
    has carried services, every `ros2 service call` to a service whose *request*
    message has no fields (`std_srvs/srv/Trigger`, `std_srvs/srv/Empty`,
    `nav2_msgs/srv/Toggle`) timed out on the caller with "no answer within 5.0 s
    - the service did not run", while the daemon counted a failure and never a
    call. `/sim/reset` therefore did nothing, and the next full run started from
    wherever the previous one had parked the car - which is exactly the state
    "Start Coordinates was outside bounds" comes from. Evidence 59 in
    bridge/docs/03-validation.md has the root cause.

    It is a check now, and it asserts the *effect* rather than the exit code:
    the response has to say `success: true` and `/odom` has to come back at the
    origin with nothing commanding the wheels.
    """
    proc = ros2("service", "call", "/sim/reset", "std_srvs/srv/Trigger", timeout=60)
    if proc.returncode != 0:
        result.detail = "/sim/reset exited %d: %s" % (
            proc.returncode, (proc.stderr.strip() or proc.stdout.strip())[-300:])
        return
    result.data["response"] = proc.stdout.strip()
    if not RESPONSE_OK.search(proc.stdout):
        result.detail = "the sim answered, but not with success: true"
        return

    x, y, linear = odom_pose()
    result.data.update({"x": x, "y": y, "linear_x": linear})
    if max(abs(x), abs(y)) > 0.25:
        result.detail = "the car is still (%.2f, %.2f) after /sim/reset" % (x, y)
        return
    if abs(linear) > 1e-9:
        result.detail = "the car is back but still commanded (linear.x=%.3f)" % linear
        return
    result.detail = "back at (%.3f, %.3f), nothing commanding the wheels" % (x, y)
    result.ok = True


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--quick", action="store_true",
                        help="skip the nav2 demo and the SLAM lap (criteria 1 and 2 only)")
    parser.add_argument("--keep-daemon", action="store_true",
                        help="do not stop the daemon, so criterion 1 is not a cold start")
    parser.add_argument("--goal", default="auto", help="nav2 demo goal (default auto)")
    parser.add_argument("--demo-timeout", type=float, default=150.0)
    parser.add_argument("--report", default=str(DEFAULT_REPORT),
                        help="where to write the JSON summary")
    parser.add_argument("--skip-slam", action="store_true",
                        help="skip the SLAM mapping check (criterion 4)")
    args = parser.parse_args()

    checks = [
        ("topic_list", "Windows lists the Ubuntu topics (cold start)",
         lambda r: check_topic_list(r, cold=not args.keep_daemon)),
        ("topic_info", "Windows resolves /scan to the gRPC peer", check_topic_info),
        ("scan_rate", "Mirrored /scan keeps its 10 Hz", check_scan_rate),
        ("uplink", "Windows /cmd_vel moves the Ubuntu robot", check_uplink),
        ("uplink_nav2", "the plan nav2 publishes reaches the robot (default uplink table)",
         check_uplink_nav2),
        ("cmd_vel_watchdog", "a /cmd_vel that stops arriving stops the robot",
         check_cmd_vel_watchdog),
    ]
    if not args.quick:
        checks.append(("nav2_demo", "nav2 plans a path and drives to the goal",
                       lambda r: check_nav2_demo(r, args.goal, args.demo_timeout)))
    if not args.quick and not args.skip_slam:
        checks.append(("slam", "slam_toolbox maps a lap from the bridged /scan + /tf",
                       check_slam))
    if not args.quick:
        checks.append(("reset", "the car goes back to the origin (and stays there)",
                       check_reset))

    results = []
    for name, title, fn in checks:
        result = Result(name, title)
        say("\n=== %s: %s" % (name, title))
        try:
            fn(result)
        except Exception as exc:  # a crashed check is a failed check
            result.detail = "%s: %s" % (type(exc).__name__, exc)
        results.append(result)
        say("[%s] %s (%.1f s) - %s" % ("PASS" if result.ok else "FAIL",
                                       name, result.seconds, result.detail))

    failed = [r for r in results if not r.ok]
    summary = {
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": os.environ.get("COMPUTERNAME", "windows"),
        "passed": len(results) - len(failed),
        "failed": len(failed),
        "checks": [r.as_dict() for r in results],
    }
    report = Path(args.report)
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
                      encoding="utf-8")

    say("\n%d/%d checks passed; report: %s" % (summary["passed"], len(results), report))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
