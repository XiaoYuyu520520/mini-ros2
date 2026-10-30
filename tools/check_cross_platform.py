#!/usr/bin/env python3
"""Build and test this repo on Windows *and* on the Ubuntu box, in one command.

The port is only real if both halves compile, and until this script existed that
was a table in a document that somebody had to remember to walk. Two of the
checks below are the ones that have actually caught portability bugs:

  * the same build entry point is used on both sides, so a hardcoded MSYS2 path
    or a `".exe"` spelling shows up as a failure on the other machine rather
    than in somebody's shell history months later;
  * the BehaviorTree.CPP archive the shim imports is rebuilt from source on the
    remote side, which is how "it was already there from an old manual build"
    stopped being load bearing;
  * the bridge's *control* entry point is the one file that is genuinely spelled
    twice - `bridge\\windows\\ros2_bridge.bat` and `bridge/linux/ros2_bridge.sh` -
    and the remote half of that step runs a whole lifecycle, start to stop,
    because "the script parses" is not the question. "A daemon starts on Linux"
    is.

    python tools/check_cross_platform.py            # both halves
    python tools/check_cross_platform.py --local    # this machine only
    python tools/check_cross_platform.py --remote   # the Ubuntu box only
    python tools/check_cross_platform.py --quick    # shim + shm, skip the rest
    python tools/check_cross_platform.py --push-only

The remote half needs the ssh key and host from `bridge/scripts/ubuntu_ssh.py`
(``-i ~/Desktop/app/ssh_key/id_ed25519 ubuntu@192.168.1.113``) and writes into
``~/code/CPU`` there, which is what the device is already set up for. Only the
slices a build needs are pushed; build trees are never sent. The documents travel
with them: a slice that compiles but cannot explain itself is how `nav2/ros2_shim`
went 909 files without a README, and `tools/check_docs.py` /
`tools/check_structure.py` are two of the gates CI runs on both platforms, so the
remote half has to be able to run them.
"""
from __future__ import annotations

import argparse
import base64
import io
import os
import pathlib
import subprocess
import sys
import tarfile
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_KEY = os.path.expanduser(r"~/Desktop/app/ssh_key/id_ed25519")
DEFAULT_TARGET = "ubuntu@192.168.1.113"
SSH_KEY = DEFAULT_KEY
SSH_TARGET = DEFAULT_TARGET
REMOTE_ROOT = "~/code/CPU"

# Directories that have to exist on the other machine for the trees below to
# configure. `nav2/third_party` and `nav2/nav2_extracted` are the bulky ones
# (~220 MB together, ~10 s over the LAN) and they only go over because the shim
# really does compile from them.
PUSH = [
    "tools",
    "nav2/ros2_shim",
    "nav2/shm_transport",
    "nav2/slam_toolbox_port",
    "nav2/third_party",
    "nav2/nav2_extracted",
    "bridge/scripts",
    "bridge/linux",
    "bridge/windows",
    "bridge/tests",
    "gRPC/ros2_gRPC_sdk",
    # The documents, and the two areas the *gates* read while no build compiles
    # them: `nav2/patches` is what `verify_patch.py` checks and what the shim's
    # CMakeLists configures against, and `bridge/ubuntu/ros2_bridge_sim` owns the
    # simulator's pytest tree (the fifth test tree). Both were quietly missing
    # from this list, which is why the remote half could not run two of the gates
    # CI runs on both platforms.
    "README.md",
    "BUILD.md",
    ".github",
    ".scratch/README.md",
    "bridge/README.md",
    "bridge/docs",
    "bridge/ubuntu/ros2_bridge_sim",
    "nav2/README.md",
    "nav2/patches",
    # The application layer: the car's ROS2 workspace. It is what the
    # `racecar-build` step below builds on the other side, and a step whose
    # subject is not pushed is a step that tests nothing.
    "code/Desktop/racecar",
]

# `bridge/scripts` is in the list because the bridge's own build entry point has
# to be there for its step to mean anything: without it the remote half failed
# with "can't open file '.../bridge/scripts/build_bridge.py'", which says
# nothing about whether the bridge builds on Linux - which is the thing the
# step exists to find out.
# `build` / `install` / `log` are the colcon workspace directories and they only
# count as such at the *top* of a pushed tree: `include/gmapping/log/` is source,
# and skipping it is how a vendored package ends up unable to compile at all
# (`bridge/docs/04-gaps.md` section 28 item 17).
TOP_LEVEL_SKIP = {"build", "install", "log"}
ANY_LEVEL_SKIP = {"__pycache__", ".git", ".scratch", ".venv", "node_modules"}


class Step:
    """One row of the table: the same check, spelled for each platform."""

    def __init__(self, key: str, title: str, local: list[str], remote: str,
                 quick: bool = False, slow: bool = False) -> None:
        self.key = key
        self.title = title
        self.local = local
        self.remote = remote
        self.quick = quick
        self.slow = slow


def python() -> str:
    return sys.executable


def steps() -> list[Step]:
    py = python()
    shim_targets = ["test_messages"]
    return [
        Step("shim-build", "shim: build entry point",
             [py, "nav2/ros2_shim/build.py"] + shim_targets,
             "python3 nav2/ros2_shim/build.py " + " ".join(shim_targets),
             quick=True),
        # The remote half builds before it tests, and that is not symmetry for
        # its own sake. A bare `ctest` there reads whatever `nav2/ros2_shim/build`
        # happens to hold, so a clone that was never fully built reports
        # "Unable to find executable" for every target nobody compiled and the
        # step reads as a portability failure of the code instead of a missing
        # build. Building through the same entry point the step above uses makes
        # the result a statement about the tree rather than about this machine's
        # build directory - and the entry point is itself one of the things
        # these two rows exist to check.
        Step("shim-test", "shim: ctest",
             [py, "tools/run_ctest.py", "shim"],
             "python3 nav2/ros2_shim/build.py --jobs 6 && "
             "ctest --test-dir nav2/ros2_shim/build --output-on-failure",
             slow=True),
        Step("shm-test", "shm: ctest",
             [py, "tools/run_ctest.py", "shm"],
             "ctest --test-dir nav2/shm_transport/build --output-on-failure",
             quick=True),
        Step("bridge-build", "bridge: build entry point + ctest",
             [py, "bridge/scripts/build_bridge.py", "--test"],
             "python3 bridge/scripts/build_bridge.py --test",
             slow=True),
        # Not `quick`: this step needs the daemon the bridge-build step above
        # produces, and `--quick` may run on a machine that has never built it.
        #
        # The remote side is a full start / pid / stop / no-pid cycle, and then
        # one more start/stop that leaves a *second* assertion behind: that the
        # words after the verb reach the daemon. They did not - `extra` was
        # built from the environment only, and `"$@"` was handed on by the
        # `run` branch alone - so `ros2_bridge.sh start --mirror /scan` echoed
        # the option in "starting daemon ..." and then started a daemon with
        # every topic mirrored. Same command, same exit code 0, nothing done;
        # the `.bat` twin collected them all along. Nothing in this file looked
        # at it, which is why it is looked at now: /proc/<pid>/cmdline.
        #
        # It leaves a daemon that is already running alone rather than stopping
        # somebody else's bridge, which is the one thing a gate on a shared box
        # must not do - and that also means the extra-options half is skipped,
        # not silently passed, when somebody else's daemon is up.
        Step("bridge-ctl", "bridge: control entry point",
             ["cmd", "/c", "bridge\\windows\\ros2_bridge.bat", "status"],
             "bridge() { bash bridge/linux/ros2_bridge.sh \"$@\"; }; "
             "if bridge pid >/dev/null 2>&1; then "
             "echo 'a daemon is already running; leaving it alone'; "
             "else bridge start && "
             "bridge pid >/dev/null && "
             "bridge stop && "
             "! bridge pid >/dev/null 2>&1 && "
             "bridge start --mirror /scan --mirror /tf && "
             "pid=\"$(bridge pid)\" && "
             "cmdline=\"$(tr '\\0' ' ' < /proc/$pid/cmdline)\" && "
             "case \"$cmdline\" in *'--mirror /scan'*) ;; *) "
             "echo \"extra options never reached the daemon: $cmdline\" >&2; exit 1;; esac && "
             "case \"$cmdline\" in *'--mirror /tf'*) ;; *) "
             "echo \"extra options never reached the daemon: $cmdline\" >&2; exit 1;; esac && "
             "echo \"start forwards its options: $cmdline\" && "
             "bridge stop; fi"),
        # The car's workspace. Windows cannot build it - there is no ROS2 on this
        # machine, and the mission scripts run through the shim instead - so the
        # two halves are deliberately not the same check. Here it is
        # `check_mission_scripts.py`: every script has to load under the shim and
        # answer `waypoint_file()` with a route that is on disk, which is also
        # what catches a script that slipped back to reading the route's third
        # column as an altitude (03-validation.md evidence 80). There it is the
        # `colcon build`.
        #
        # The remote half builds the *whole* tree, from scratch. It used to name
        # the two packages mapping and navigation need, because this checkout's
        # `openslam_gmapping` had no `log/` module and could not configure at all
        # (04-gaps.md section 28 item 17); with that fixed the whole tree is six
        # packages in about 40 s, and the four that are neither of those two are
        # not decoration - `lslidar_driver` is how a real lidar arrives, and
        # `slam_gmapping` is the mapping half of the goal. `rm -rf build install
        # log` first, because an incremental build goes green on a tree that no
        # longer configures from scratch, which is what an undeclared dependency
        # looks like. The assertions after it are the other half of
        # `install(PROGRAMS ...)`: the seventeen scripts, the route CSVs and the
        # `nav.yaml` all have to be in the install tree, because that is the tree
        # `ros2 run racecar ...` reads. The names are counted rather than listed
        # because two of them have spaces in them and one is Chinese, and this
        # command travels through ssh's own shell.
        #
        # The Ubuntu box needs these once, for `lslidar_driver`:
        #   sudo apt-get install -y libpcl-dev libpcap-dev
        #       ros-jazzy-diagnostic-updater ros-jazzy-pcl-conversions
        #       ros-jazzy-pluginlib
        Step("racecar-build", "racecar: colcon build (remote) / scripts (local)",
             [py, "code/Desktop/racecar/tools/check_mission_scripts.py"],
             "source /opt/ros/jazzy/setup.bash && cd code/Desktop/racecar && "
             "rm -rf build install log && "
             "colcon build --parallel-workers 6 && "
             "test \"$(ls -1 install/racecar/lib/racecar/*.py | wc -l)\" = 17 && "
             "test -x install/racecar/lib/racecar/go.py && "
             "test -f install/racecar/lib/racecar/sim_room12.csv && "
             "test -f install/racecar/share/racecar/config/nav.yaml && "
             "echo 'installed: 17 mission scripts, the routes, nav.yaml'"),
        Step("grpc-build", "grpc sdk: build entry point + ctest",
             [py, "gRPC/ros2_gRPC_sdk/scripts/build.py", "--test"],
             "python3 gRPC/ros2_gRPC_sdk/scripts/build.py --test",
             slow=True),
    ]


def log(message: str) -> None:
    print("[check] %s" % message, flush=True)


def ssh(argv_script: str, timeout: float) -> tuple[int, str]:
    argv = ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "BatchMode=yes",
            "-i", SSH_KEY, SSH_TARGET, argv_script]
    try:
        done = subprocess.run(argv, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, "timed out after %gs" % timeout
    text = done.stdout.decode("utf-8", "replace") + done.stderr.decode("utf-8", "replace")
    return done.returncode, text


def remote_available() -> bool:
    code, _ = ssh("true", 30)
    return code == 0


def push(timeout: float) -> int:
    buf = io.BytesIO()
    count = 0
    with tarfile.open(fileobj=buf, mode="w:gz", compresslevel=1) as tar:
        for rel in PUSH:
            base = REPO_ROOT / rel
            if not base.exists():
                log("skip (not in this checkout): %s" % rel)
                continue
            if base.is_file():
                # A single document that has to be there for the tree to explain
                # itself - `.scratch/README.md` is the rules for the evidence
                # store, while the store itself is 75 MB that must not travel.
                tar.add(str(base), arcname=rel)
                count += 1
                continue
            for path in sorted(base.rglob("*")):
                sub = path.relative_to(base)
                if path.is_dir() or sub.parts[0] in TOP_LEVEL_SKIP:
                    continue
                if ANY_LEVEL_SKIP & set(sub.parts):
                    continue
                tar.add(str(path), arcname="%s/%s" % (rel, sub.as_posix()))
                count += 1
    data = buf.getvalue()
    log("pushing %d files, %.1f MB" % (count, len(data) / 1e6))
    argv = ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "BatchMode=yes",
            "-i", SSH_KEY, SSH_TARGET,
            "mkdir -p %s && tar -xzf - -C %s && echo pushed" % (REMOTE_ROOT, REMOTE_ROOT)]
    done = subprocess.run(argv, input=data, timeout=timeout)
    return done.returncode


def run_local(step: Step, timeout: float) -> tuple[bool, str]:
    began = time.monotonic()
    env = dict(os.environ)
    env["PYTHONUTF8"] = "1"
    done = subprocess.run([str(part) for part in step.local], cwd=str(REPO_ROOT),
                          env=env, capture_output=True, timeout=timeout)
    text = done.stdout.decode("utf-8", "replace") + done.stderr.decode("utf-8", "replace")
    ok = done.returncode == 0
    print_verdict(step, "local", ok, time.monotonic() - began, text)
    return ok, text


def run_remote(step: Step, timeout: float) -> tuple[bool, str]:
    began = time.monotonic()
    script = "cd %s && %s" % (REMOTE_ROOT, step.remote)
    code, text = ssh(script, timeout)
    ok = code == 0
    print_verdict(step, "remote", ok, time.monotonic() - began, text)
    return ok, text


def print_verdict(step: Step, side: str, ok: bool, seconds: float, text: str) -> None:
    print("[check] %-6s %-34s %s  (%.1fs)"
          % (side, step.title, "PASS" if ok else "FAIL", seconds), flush=True)
    if not ok:
        tail = [line for line in text.splitlines() if line.strip()][-25:]
        for line in tail:
            print("           | %s" % line.rstrip()[:200])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--local", action="store_true", help="only this machine")
    parser.add_argument("--remote", action="store_true", help="only the Ubuntu box")
    parser.add_argument("--quick", action="store_true",
                        help="only the fast checks (build entry points, shm)")
    parser.add_argument("--only", default="",
                        help="comma list of step keys to run (default: all of them)")
    parser.add_argument("--push-only", action="store_true")
    parser.add_argument("--key", default=DEFAULT_KEY)
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--timeout", type=float, default=1800.0, help="per step, seconds")
    args = parser.parse_args()

    global SSH_KEY, SSH_TARGET
    SSH_KEY, SSH_TARGET = args.key, args.target

    do_local = args.local or not args.remote
    do_remote = args.remote or not args.local
    only = {key.strip() for key in args.only.split(",") if key.strip()}
    if only:
        known = {s.key for s in steps()}
        unknown = sorted(only - known)
        if unknown:
            log("no such step: %s (have: %s)" % (", ".join(unknown),
                                                 ", ".join(sorted(known))))
            return 2
    selected = [s for s in steps()
                if (not only or s.key in only) and (not args.quick or s.quick)]
    failures: list[str] = []

    if do_remote:
        if not remote_available():
            log("no ssh to %s -- skipping the remote half (--local for this machine)"
                % SSH_TARGET)
            do_remote = False
        else:
            if push(args.timeout) != 0:
                log("push failed")
                return 1
            if args.push_only:
                return 0

    for step in selected:
        if do_local:
            ok, _ = run_local(step, args.timeout)
            if not ok:
                failures.append("local:%s" % step.key)
        if do_remote:
            ok, _ = run_remote(step, args.timeout)
            if not ok:
                failures.append("remote:%s" % step.key)

    print()
    if failures:
        print("[check] %d failure(s): %s" % (len(failures), ", ".join(failures)))
        return 1
    print("[check] both halves green (%d steps)" % len(selected))
    return 0


if __name__ == "__main__":
    sys.exit(main())