#!/usr/bin/env python3
"""Drive the whole cross-language interoperability matrix and print it as a table.

`tests/py_interop.cpp` and `python/py_interop.py` are two independent
implementations of the same canonical `nav_msgs/msg/Path`, one in the shim's C++
and one on top of the ctypes binding. Both have four modes -- dump, check, pub,
sub -- and the contract that matters is every pairing between them:

    leg 1  cpp dump  -> py check    the two CDR writers agree
    leg 2  py dump   -> cpp check   and agree in the other direction
    leg 3  byte identity            the two dumps are the same bytes, not just both readable
    leg 4  cpp pub   -> py sub      a channel written by C++ is readable in Python
    leg 5  py pub    -> cpp sub     and a channel written by Python is readable in C++

    leg 6  cpp sizes == py sizes    one decision, two spellings, same numbers
    leg 7  sim time                 `shim_run.py --sim-time` reaches the script's node
    leg 8  scalar arrays            a `float32[]` is a list, not one float

Leg 8 is a fixture rather than a pairing, and it exists because the Python
generator once wrote `f_scalar` for every array: `float32[] ranges` was one
float, `int8[] data` one byte, and `uint8[16] uuid` - the goal id every action
message carries - one byte, so a sixteen-byte identifier decoded as a small
integer and every field after it came out shifted. The payload the leg compares
against is one a live `navigate_to_pose` action server published (its
`_action/status`, an EXECUTING goal), so the bytes are the shim's own, not a
second guess at them.

Leg 7 is the one leg that is not about the two implementations of a message:
it pins the way `use_sim_time` reaches a mission script. `nav.yaml` turns the
parameter on for every node in the stack, so the stamps a script reads off the
wire are the simulator's seconds - a script that compares one of those against
its own `get_clock().now()` is on the wall clock's epoch unless the choice came
through, and `go.py` filters its own argv on top of that, which a `--ros-args
-p use_sim_time:=true` written into it does not survive. The environment
variable is the way in that no script can drop, and this leg checks that it
works, that the command line still beats it, and that neither one moves a
script that asked for nothing.

Legs 1 and 2 are also compared byte for byte: "both sides deserialize each
other" would still hide two different encodings of the same value, so the two
303-byte dumps have to be identical files.

Until this driver existed the matrix was run by hand, which made it the one
piece of the shim that could regress without anyone noticing (see
bridge/docs/05-audit.md). Legs 4 and 5 cross a process boundary *and* a language
boundary, so they also pin the shared-memory ABI: the ring the C++ side creates
has to be openable through ctypes with no shared header.

    python nav2/ros2_shim/python/interop_matrix.py --cxx <build>/py_interop.exe

Channel names carry the driver's pid, so two runs cannot collide, and they are
bare names -- no topic namespace -- because this exercises shm_transport
directly rather than the shim's topic mapping.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import threading
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
sys.path.insert(0, os.path.join(_HERE, "..", "..", "shm_transport", "python"))

import py_interop  # noqa: E402  (canonical_path / verify_canonical / dump / check)
from ros2py import channel  # noqa: E402  (slot_size_for_topic)
from shm_transport import ShmChannel  # noqa: E402

SLOT_SIZE = 4096
PUBLISH_SECONDS = 3.0


class Leg:
    def __init__(self, name, what):
        self.name = name
        self.what = what
        self.ok = None
        self.detail = ""


def run_cxx(cxx, *args):
    return subprocess.run([cxx, *args], capture_output=True, text=True,
                          timeout=PUBLISH_SECONDS + 30.0)


def leg_cpp_dump_py_check(cxx, tmp, leg):
    """cpp dump -> py check"""
    path = os.path.join(tmp, "cpp.bin")
    proc = run_cxx(cxx, "dump", path)
    if proc.returncode != 0:
        leg.ok = False
        leg.detail = "cpp dump rc=%d %s" % (proc.returncode, proc.stderr.strip())
        return None
    rc = py_interop.check(path)
    leg.ok = rc == 0
    leg.detail = "cpp dumped %d bytes, py check rc=%d" % (os.path.getsize(path), rc)
    return path


def leg_py_dump_cpp_check(cxx, tmp, leg):
    """py dump -> cpp check"""
    path = os.path.join(tmp, "py.bin")
    rc = py_interop.dump(path)
    if rc != 0:
        leg.ok = False
        leg.detail = "py dump rc=%d" % rc
        return None
    proc = run_cxx(cxx, "check", path)
    leg.ok = proc.returncode == 0
    leg.detail = "py dumped %d bytes, cpp check rc=%d" % (os.path.getsize(path), proc.returncode)
    return path


def leg_byte_identity(leg, cpp_path, py_path):
    cpp = open(cpp_path, "rb").read()
    py = open(py_path, "rb").read()
    leg.ok = cpp == py
    leg.detail = "cpp %d bytes, py %d bytes, identical=%s" % (len(cpp), len(py), cpp == py)
    if cpp != py:
        first = next((i for i, (a, b) in enumerate(zip(cpp, py)) if a != b),
                     min(len(cpp), len(py)))
        leg.detail += ", first difference at byte %d" % first


def open_with_retry(channel, deadline_s):
    """A reader has to wait for the writer's `create` to land in the first place."""
    end = time.time() + deadline_s
    last = None
    while time.time() < end:
        try:
            return ShmChannel.open(channel)
        except Exception as exc:  # noqa: BLE001  (the channel does not exist yet)
            last = exc
            time.sleep(0.02)
    raise RuntimeError("channel %s never appeared: %s" % (channel, last))


def leg_cpp_pub_py_sub(cxx, leg):
    channel = "interop_cpp2py_%d" % os.getpid()
    publisher = subprocess.Popen([cxx, "pub", channel, str(SLOT_SIZE)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True)
    try:
        ch = open_with_retry(channel, PUBLISH_SECONDS)
        data = None
        deadline = time.time() + PUBLISH_SECONDS
        while time.time() < deadline and not data:
            data = ch.subscribe(capacity=SLOT_SIZE, timeout_ms=200)
        ch.close()
    finally:
        out, _err = publisher.communicate(timeout=30)
    if not data:
        leg.ok = False
        leg.detail = "py sub read nothing; cpp pub said %r" % out.strip()
        return
    msg = py_interop.Path.from_bytes(data)
    leg.ok = py_interop.verify_canonical(msg) and publisher.returncode == 0
    leg.detail = "py sub read %d bytes / %d poses, cpp pub rc=%d" % (
        len(data), len(msg.poses), publisher.returncode)


def leg_py_pub_cpp_sub(cxx, leg):
    channel = "interop_py2cpp_%d" % os.getpid()
    ch = ShmChannel.create(channel, 16, SLOT_SIZE)
    payload = py_interop.canonical_path().serialize()
    stop = threading.Event()
    sent = [0]

    def pump():
        while not stop.is_set():
            if ch.publish(payload):
                sent[0] += 1
            stop.wait(0.05)

    thread = threading.Thread(target=pump, daemon=True)
    thread.start()
    try:
        time.sleep(0.2)  # let the ring exist before the C++ reader opens it
        proc = run_cxx(cxx, "sub", channel, str(SLOT_SIZE), "3000")
    finally:
        stop.set()
        thread.join(timeout=5)
        ch.close()
    leg.ok = proc.returncode == 0
    leg.detail = "py pub sent %d, cpp sub rc=%d %s" % (
        sent[0], proc.returncode, (proc.stdout or "").strip())


# The sizing rule, one name per branch: a plain topic, a grid, the two
# near-misses (`..._info` and a parameter service's `_request` path), the two
# parameter-shaped names that are not services, and the action status topic.
# `shm_config.hpp` and `ros2py/channel.py` are two spellings of one decision -
# and a subscriber that opens at a different slot size from its publisher
# either commits the wrong amount of pagefile or silently drops every message.
SIZE_PROBE_NAMES = [
    "/scan",
    "/cmd_vel",
    "/map",
    "/global_costmap/costmap",
    "/local_costmap/voxel_grid",
    "/keepout_costmap_filter_info",
    "/parameter_events",
    "/rosout",
    "/navigate_to_pose/_action/status",
    "/controller_server/get_parameters",
    "/controller_server/get_parameters/_request",
]


def leg_slot_size_agreement(cxx, leg):
    """cpp sizes -> py sizes"""
    proc = run_cxx(cxx, "sizes", *SIZE_PROBE_NAMES)
    if proc.returncode != 0:
        leg.ok = False
        leg.detail = "cpp sizes rc=%d %s" % (proc.returncode, proc.stderr.strip())
        return
    cpp = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2:
            cpp[parts[0]] = int(parts[1])
    mismatched = []
    for name in SIZE_PROBE_NAMES:
        mine = channel.slot_size_for_topic(name)
        if cpp.get(name) != mine:
            mismatched.append("%s cpp=%s py=%s" % (name, cpp.get(name), mine))
    leg.ok = not mismatched and len(cpp) == len(SIZE_PROBE_NAMES)
    leg.detail = ("%d names agree" % len(SIZE_PROBE_NAMES)) if leg.ok else "; ".join(mismatched)


# A mission script in miniature: it takes a bare argument (the CSV one), calls
# `rclpy.init()` with its own argv filtered the way `go.py` filters it, and
# answers with the parameter a node of it ends up holding. The filtering is the
# point - the `--ros-args` tail is kept whole, so an explicit `-p` keeps its
# value, and the bare argument still does not reach rclpy.
SIM_TIME_PROBE = """import sys

import rclpy
from rclpy.node import Node
from ros2py import clock as ros2py_clock, node as ros2py_node

argv = sys.argv
tail = []
if '--ros-args' in argv:
    cut = argv.index('--ros-args')
    tail, argv = argv[cut:], argv[:cut]
rclpy.init(args=[argv[0]] + [a for a in argv[1:] if a.startswith('-')] + tail)

node = Node('sim_time_probe')
overrides = ros2py_node.parameter_overrides()
print('PROBE override=%s sim=%s declared=%s'
      % (overrides.get('use_sim_time'), ros2py_clock.use_sim_time(),
         node.has_parameter('use_sim_time')))
rclpy.shutdown()
"""


def sim_time_probe_answer(tmp, flags, arguments, environment):
    """What the miniature script's node says, for one set of arguments.

    The arguments are laid out the way the runner takes them - flags for the
    runner, then the script, then everything the script itself gets - because
    that order is part of the contract `racecar_ctl.py run` builds on.
    """
    probe = os.path.join(tmp, "sim_time_probe.py")
    with open(probe, "w", encoding="utf-8") as handle:
        handle.write(SIM_TIME_PROBE)
    env = dict(os.environ, NAV2_SHM_GRAPH_DIR=os.path.join(tmp, "graph"))
    env.pop("ROS2_SHIM_SIM_TIME", None)
    env.update(environment)
    proc = subprocess.run(
        [sys.executable, os.path.join(_HERE, "shim_run.py")] + flags + [probe] + arguments,
        capture_output=True, text=True, env=env, timeout=60.0)
    for line in proc.stdout.splitlines():
        if line.startswith("PROBE "):
            return line[len("PROBE "):].strip()
    return "rc=%d %s" % (proc.returncode, (proc.stderr or "").strip()[-200:])


def leg_sim_time(leg, tmp):
    """shim_run.py --sim-time -> the script's node"""
    # name, runner flags, the script's own arguments, the environment
    cases = [
        ("--sim-time", ["--sim-time"], [], {}),
        ("--wall-time", ["--wall-time"], [], {}),
        ("--sim-time + -p false",
         ["--sim-time"], ["--ros-args", "-p", "use_sim_time:=false"], {}),
        ("--wall-time + -p true",
         ["--wall-time"], ["--ros-args", "-p", "use_sim_time:=true"], {}),
        ("no flag", [], [], {}),
        ("ROS2_SHIM_SIM_TIME=1", [], [], {"ROS2_SHIM_SIM_TIME": "1"}),
    ]
    # What each case has to answer: the override that reached `rclpy.init`, and
    # the clock the node ended up on. Both halves matter - an override the node
    # ignores is as broken as one that never arrived - and so does the one case
    # where they are supposed to stay off, because every script that never
    # mentions the parameter depends on that being the default.
    expected = {
        "--sim-time": "override=True sim=True declared=True",
        "--wall-time": "override=False sim=False declared=True",
        "--sim-time + -p false": "override=False sim=False declared=True",
        "--wall-time + -p true": "override=True sim=True declared=True",
        "no flag": "override=None sim=False declared=False",
        "ROS2_SHIM_SIM_TIME=1": "override=True sim=True declared=True",
    }
    problems = []
    for name, flags, arguments, environment in cases:
        got = sim_time_probe_answer(tmp, flags, arguments, environment)
        if got != expected[name]:
            problems.append("%s: %s (wanted %s)" % (name, got, expected[name]))
    leg.ok = not problems
    leg.detail = ("6 cases agree" if leg.ok else "; ".join(problems))


# `<action>/_action/status`, exactly as `bt_navigator` published it: one goal,
# goal_id 01000000..., stamp 15145.120000238, status 2 (EXECUTING). Byte for
# byte: the array length, the 16-byte uuid, the two stamp words, the state.
ACTION_STATUS_FIXTURE = "0100000001000000000000000000000000000000293b0000ee0e270702"


def leg_scalar_arrays(leg):
    """py arrays -> the bytes the shim wrote"""
    from pymessages.action_msgs.msg.goal_status import GoalStatus
    from pymessages.action_msgs.msg.goal_status_array import GoalStatusArray
    from pymessages.nav_msgs.msg.occupancy_grid import OccupancyGrid
    from pymessages.sensor_msgs.msg.laser_scan import LaserScan
    from pymessages.unique_identifier_msgs.msg.uuid import UUID

    problems = []

    # The goal id is the one array every action message carries, and it is a
    # fixed-size one - the shape whose size the generator used to drop.
    goal = GoalStatusArray()
    entry = GoalStatus()
    entry.goal_info.goal_id.uuid = [1] + [0] * 15
    entry.goal_info.stamp.sec = 15145
    entry.goal_info.stamp.nanosec = 120000238
    entry.status = 2
    goal.status_list = [entry]
    written = goal.serialize().hex()
    if written != ACTION_STATUS_FIXTURE:
        problems.append("status bytes %s != %s" % (written, ACTION_STATUS_FIXTURE))
    if GoalStatusArray.from_bytes(bytes.fromhex(ACTION_STATUS_FIXTURE)).serialize().hex() \
            != ACTION_STATUS_FIXTURE:
        problems.append("status does not read back")
    decoded = GoalStatusArray.from_bytes(bytes.fromhex(ACTION_STATUS_FIXTURE))
    if decoded.status_list and (decoded.status_list[0].status != 2
                                or decoded.status_list[0].goal_info.stamp.sec != 15145
                                or list(decoded.status_list[0].goal_info.goal_id.uuid)[:1] != [1]):
        problems.append("status decoded as %r" % (decoded.status_list[0],))

    # A uuid on its own is sixteen bytes and a list of sixteen numbers.
    uuid = UUID()
    uuid.uuid = list(range(16))
    if len(uuid.serialize()) != 16 or UUID.from_bytes(uuid.serialize()).uuid != list(range(16)):
        problems.append("uuid is %d bytes" % len(uuid.serialize()))

    # The two variable-length ones a SLAM or nav2 Python node would publish.
    # Values that are exact in float32: the fields are float32 on the wire, and
    # 0.1 is not, so comparing against it would test the comparison and not the
    # codec.
    scan = LaserScan()
    scan.ranges = [1.0, 2.5, 3.5]
    scan.intensities = [0.5, 0.25]
    read_back = LaserScan.from_bytes(scan.serialize())
    if read_back.ranges != [1.0, 2.5, 3.5] or read_back.intensities != [0.5, 0.25]:
        problems.append("laser scan came back as %r / %r"
                        % (read_back.ranges, read_back.intensities))

    grid = OccupancyGrid()
    grid.data = [0, 100, -1, 0]
    if OccupancyGrid.from_bytes(grid.serialize()).data != [0, 100, -1, 0]:
        problems.append("occupancy grid came back as %r"
                        % OccupancyGrid.from_bytes(grid.serialize()).data)

    leg.ok = not problems
    leg.detail = ("4 payloads, one of them the stack's own" if leg.ok else "; ".join(problems))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cxx", required=True, help="path to the py_interop executable")
    ap.add_argument("--tmp", default=None, help="scratch directory (default: a fresh temp dir)")
    args = ap.parse_args(argv)

    if not os.path.isfile(args.cxx):
        print("interop matrix: %s not found -- build py_interop first" % args.cxx,
              file=sys.stderr)
        return 2

    tmp = args.tmp or tempfile.mkdtemp(prefix="interop_")
    os.makedirs(tmp, exist_ok=True)

    legs = [
        Leg("1  cpp dump -> py check", "the two writers agree on the CDR layout"),
        Leg("2  py dump  -> cpp check", "and agree in the other direction"),
        Leg("3  byte identity", "the two dumps are the same bytes, not merely equivalent"),
        Leg("4  cpp pub  -> py sub", "a C++ ring is readable through ctypes"),
        Leg("5  py pub   -> cpp sub", "a ctypes ring is readable from C++"),
        Leg("6  cpp sizes == py sizes", "the two spellings of the channel-sizing rule agree"),
        Leg("7  sim time", "shim_run.py --sim-time reaches the script's node, -p beats it"),
        Leg("8  scalar arrays", "a sequence field is a list on the wire, not one value"),
    ]

    cpp_path = leg_cpp_dump_py_check(args.cxx, tmp, legs[0])
    py_path = leg_py_dump_cpp_check(args.cxx, tmp, legs[1])
    if cpp_path and py_path:
        leg_byte_identity(legs[2], cpp_path, py_path)
    else:
        legs[2].ok = False
        legs[2].detail = "skipped, an earlier leg did not produce a file"
    leg_cpp_pub_py_sub(args.cxx, legs[3])
    leg_py_pub_cpp_sub(args.cxx, legs[4])
    leg_slot_size_agreement(args.cxx, legs[5])
    leg_sim_time(legs[6], tmp)
    leg_scalar_arrays(legs[7])

    width = max(len(l.name) for l in legs)
    print("cross-language interop matrix (slot_size=%d)" % SLOT_SIZE)
    for leg in legs:
        print("  %-*s  %-4s  %s" % (width, leg.name, "PASS" if leg.ok else "FAIL", leg.detail))

    failed = [l.name for l in legs if not l.ok]
    print("interop matrix: %d/%d legs passed" % (len(legs) - len(failed), len(legs)))
    if failed:
        print("failed: %s" % ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())