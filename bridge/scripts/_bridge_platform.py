#!/usr/bin/env python3
"""Plumbing shared by the cross-platform nav2 launchers.

``run_nav2_stack.py`` and ``run_slam_demo.py`` are the Linux answers to
``bridge/windows/nav2_stack.bat`` and ``bridge/windows/slam_demo.bat``: the same
jobs, one script each, running on both platforms. Everything that differs
between the two platforms lives here, so the launchers themselves read as the
sequence of steps they perform:

* :func:`environment` - the variables ``bridge_env.bat`` / ``bridge_env.sh``
  set, built in Python. A launcher exports them into its own process tree
  instead of having to be wrapped in a shell, and they disappear with it.
* :func:`exe` - ``planner_server.exe`` on Windows, ``planner_server`` here.
* :func:`wait_bridge_ready` - delegated to the platform's own
  ``wait_bridge_ready`` script, which is the one place that knows how to start
  the daemon and how to poll its ready file.
* :func:`live_pids` / :func:`kill_nodes` - which of our nodes are running
  right now, and stopping them: the image name resolved to a running pid on
  Windows (``tasklist`` + ``taskkill``), ``/proc/<pid>/cmdline`` on Linux.
* :func:`spawn` - one background node per call, one log file per node.
* :func:`ros2` - the shim CLI (``ros2.bat`` / ``ros2.sh``).
* :func:`preset_params` / :func:`wait_for_change_state` /
  :func:`configure_and_activate` - the three steps every script that starts a
  lifecycle node of its own repeats: the parameter preset that steps aside for
  the caller, the wait for ``/<node>/change_state`` to answer, and the
  configure(1) + activate(3) pair the stack's lifecycle manager would
  otherwise drive.

Both platforms build into ``bridge/windows/build`` and ``nav2/ros2_shim/build``
- the directory names are historical and shared on purpose, so that no document
and no path here has to be spelled two ways.
"""
from __future__ import annotations

import csv
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = os.name == "nt"

WINDOWS_DIR = REPO_ROOT / "bridge" / "windows"
LINUX_DIR = REPO_ROOT / "bridge" / "linux"

# Where the ported nav2 servers, nav2_goal and the slam port land. `build/` is
# the shim's own default; nav2/ros2_shim/build.py builds into it unless told
# otherwise, and both platforms use the same spelling.
SHIM_BUILD = REPO_ROOT / "nav2" / "ros2_shim" / "build"
OVERRIDES_YAML = REPO_ROOT / "nav2" / "ros2_shim" / "config" / "nav2_shim_overrides.yaml"
SAFETY_YAML = REPO_ROOT / "nav2" / "ros2_shim" / "config" / "nav2_shim_safety.yaml"
SAFETY_CHAIN_YAML = REPO_ROOT / "nav2" / "ros2_shim" / "config" / "nav2_shim_safety_chain.yaml"
SHIM_SRC = REPO_ROOT / "nav2" / "nav2_extracted" / "navigation2-1.5.1"
STACK_PARAMS_YAML = SHIM_SRC / "nav2_bringup" / "params" / "nav2_params.yaml"
DOCKING_ROUTE_YAML = REPO_ROOT / "nav2" / "ros2_shim" / "config" / "nav2_shim_docking_route.yaml"

# The pair upstream's launch file puts *after* the controller. Neither is in the
# stack's `lifecycle_manager.node_names`, so whoever starts them has to drive
# their own configure/activate - which is what run_nav2_stack.py does for
# `--safety` and run_nav2_safety.py does on its own.
SAFETY_NODES = ["velocity_smoother", "collision_monitor"]
DOCKING_ROUTE_NODES = ["docking_server", "route_server"]

# This desk's MSYS2 is only a fallback: the bridge build copies the MinGW and
# gRPC/protobuf DLLs next to the binaries (bridge/windows/cmake/
# bundle_runtime.cmake), so PATH is not what makes them run any more.
DEFAULT_MSYS2 = Path(r"D:\WindowsServer2025_1\software\MSYS2")

# `python` in the instructions this repo prints, spelled for the platform that
# is reading them.
PYTHON = "python" if IS_WINDOWS else "python3"


def environment() -> dict:
    """The environment ``bridge_env.bat`` / ``bridge_env.sh`` would export.

    Every value is an override-or-default, so anything already set in the
    caller's environment wins - `ROS2_BRIDGE_AGENT=192.168.1.113:50051
    python bridge/scripts/run_nav2_stack.py ...` is the usual one.
    """
    env = os.environ.copy()

    if IS_WINDOWS and env.get("ROS2_BRIDGE_NO_MSYS2_PATH") != "1":
        msys2 = env.get("ROS2_BRIDGE_MSYS2") or env.get("CPU_MSYS2_ROOT") or str(DEFAULT_MSYS2)
        ucrt = Path(msys2) / "ucrt64" / "bin"
        if ucrt.is_dir():
            env["PATH"] = str(ucrt) + os.pathsep + env.get("PATH", "")

    env.setdefault("ROS2_BRIDGE_CPU_ROOT", str(REPO_ROOT))
    env.setdefault("ROS2_BRIDGE_BUILD", str(WINDOWS_DIR / "build"))

    # Shared-memory geometry: publisher and subscriber have to agree, so every
    # process of this tree has to see the same numbers.
    env.setdefault("NAV2_SHM_SLOT_COUNT", "8")
    env.setdefault("NAV2_SHM_SLOT_SIZE", str(2 * 1024 * 1024))
    env.setdefault("NAV2_SHM_LARGE_SLOT_SIZE", str(8 * 1024 * 1024))

    # Where the ported nav2 packages are. The ament shim answers
    # `$(find-pkg-share <pkg>)` - the token nav2's own params file puts in
    # `bt_search_directories` - out of NAV2_SHIM_SRC, because the port builds
    # the extracted sources in place instead of installing a share tree.
    env.setdefault("NAV2_SHIM_SRC", str(SHIM_SRC))
    env.setdefault("NAV2_SHIM_BT_DIR", str(REPO_ROOT / "nav2" / "ros2_shim" / "config" / "bt"))

    env.setdefault("NAV2_SHM_GRAPH_DIR", str(Path(tempfile.gettempdir()) / "nav2_shim_graph"))
    env.setdefault("ROS2_SHM_GRAPH_DIR", env["NAV2_SHM_GRAPH_DIR"])
    env.setdefault("NAV2_SHM_LOG_LEVEL", "info")

    env.setdefault("ROS2_GRPC_SDK_CONFIG_DIR", str(REPO_ROOT / "gRPC" / "ros2_gRPC_sdk" / "config"))

    env.setdefault("ROS2_BRIDGE_AUTOSTART", "1")
    env.setdefault("ROS2_BRIDGE_READY_FILE",
                   str(WINDOWS_DIR / "build" / "ros2_bridge_daemon.ready"))
    env.setdefault("ROS2_BRIDGE_WAIT_MS", "30000")
    env.setdefault("ROS2_BRIDGE_LOG", str(WINDOWS_DIR / "build" / "ros2_bridge_daemon.log"))
    env.setdefault("ROS2_BRIDGE_ERR", str(WINDOWS_DIR / "build" / "ros2_bridge_daemon.err.log"))
    env.setdefault("ROS2_BRIDGE_LOG_MAX", str(16 * 1024 * 1024))
    return env


def exe(name: str) -> Path:
    """The built binary called *name*, with this platform's suffix."""
    return SHIM_BUILD / (name + ".exe" if IS_WINDOWS else name)


def missing_exes(names) -> list:
    return [p for p in (exe(n) for n in names) if not p.is_file()]


def build_hint() -> str:
    return "build it with:  %s nav2/ros2_shim/build.py" % PYTHON


def wait_bridge_ready(env: dict) -> int:
    """Make sure the daemon is up and done with its first scan.

    The platform's own script does the work - it is the one that knows whether
    the daemon is started with `tasklist` + `ros2_bridge.bat` or with
    `ros2_bridge.sh` - and it returns 0 when there is something to talk to.
    """
    if IS_WINDOWS:
        argv = ["cmd", "/c", str(WINDOWS_DIR / "wait_bridge_ready.bat")]
    else:
        argv = ["bash", str(LINUX_DIR / "wait_bridge_ready.sh")]
    return subprocess.run(argv, cwd=str(REPO_ROOT), env=env).returncode


def ros2(args, env: dict, capture: bool = False):
    """Run the shim CLI, which autostarts the daemon unless told not to."""
    if IS_WINDOWS:
        argv = ["cmd", "/c", str(WINDOWS_DIR / "ros2.bat")] + [str(a) for a in args]
    else:
        argv = ["bash", str(LINUX_DIR / "ros2.sh")] + [str(a) for a in args]
    return subprocess.run(argv, cwd=str(REPO_ROOT), env=env, capture_output=capture, text=True)


def _linux_pids(names) -> list:
    """(pid, name) for every live process running one of our shim binaries.

    Read out of /proc rather than parsed out of `ps`: the answer is exact, and
    a name that merely appears in somebody else's command line - a shell
    running this script, say - cannot be mistaken for a node.
    """
    wanted = set(names)
    found = []
    proc_root = Path("/proc")
    if not proc_root.is_dir():
        return found
    build = str(SHIM_BUILD)
    for entry in proc_root.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            raw = (entry / "cmdline").read_bytes()
        except OSError:
            continue
        if not raw:
            continue
        argv0 = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
        name = os.path.basename(argv0)
        if name not in wanted:
            continue
        try:
            directory = os.path.dirname(os.path.realpath(argv0))
        except OSError:
            continue
        if directory == build:
            found.append((int(entry.name), name))
    return found


def _linux_alive(names) -> list:
    return [pid for pid, _ in _linux_pids(names)]


# `OpenProcess` with `QueryFullProcessImageNameW`: the pair that tells a process
# that is running from an entry an earlier `taskkill` left behind.
_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def _windows_image_path(pid: int):
    """The image a running process was started from, None for a leftover entry.

    ``wmic`` is gone from this generation of Windows and ``tasklist`` alone
    cannot see the difference: an entry whose process has already ended is
    still listed under the same image name, so anything that matches on the
    name alone counts it as a node. Asking the process itself is what settles
    it - a running one answers with its own path, a leftover fails with
    ERROR_GEN_FAILURE (31). Never called off Windows.
    """
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        size = wintypes.DWORD(32768)
        buffer = ctypes.create_unicode_buffer(size.value)
        if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer,
                                                   ctypes.byref(size)):
            return None
        return buffer.value
    finally:
        kernel32.CloseHandle(handle)


def _windows_pids(names) -> list:
    """(pid, name) for every running process running one of our binaries.

    The list is read whole and matched here rather than filtered with
    `tasklist /fi`: the filtered form answers with a localized "no tasks are
    running" line instead of an empty list, and that line is a different shape
    on every machine this runs on.

    `status eq running` was tried first for this and it is the wrong tool - on
    this machine it answers `unknown` for every process, the running ones
    included, so it drops the nodes and the leftovers together, which is how
    `racecar_ctl.py stop` came to skip every SLAM node it was asked to stop.
    """
    wanted = set(name.lower() + ".exe" for name in names)
    listing = subprocess.run(["tasklist", "/fo", "csv", "/nh"],
                             capture_output=True, text=True,
                             encoding="utf-8", errors="replace")
    found = []
    for row in csv.reader(listing.stdout.splitlines()):
        if len(row) < 2:
            continue
        image = row[0].strip().lower()
        pid = row[1].strip()
        if image not in wanted or not pid.isdigit():
            continue
        if _windows_image_path(int(pid)) is None:
            continue
        found.append((int(pid), image[:-len(".exe")]))
    return found


def live_pids(names) -> list:
    """The pids of the nodes called *names* that are running right now."""
    if IS_WINDOWS:
        return [pid for pid, _ in _windows_pids(names)]
    return _linux_alive(names)


def kill_nodes(names, note: str = "") -> bool:
    """Stop the named nodes wherever they are running. True if any were.

    By name rather than by pid: the launchers do not keep a process table, and
    these are the port's own binaries - `planner_server` is not a name anything
    else on the machine uses. The name is resolved to the pids that are really
    there before anything is killed, so an entry an earlier `taskkill /F` left
    behind is not reported as a node that was stopped.

    *note* is printed when something was in fact stopped, so `stop` says what it
    did and a fresh start stays quiet; the wording belongs to the caller, which
    is the one that knows whether it just took down a stack or two nodes.
    """
    if IS_WINDOWS:
        found = _windows_pids(names)
        if not found:
            return False
        if note:
            print(note, flush=True)
        for pid, _ in found:
            # By pid, not by image name: the leftovers match on the name too,
            # and asking to stop one of those answers "Access is denied" -
            # which says nothing about whether a node was running.
            subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                           capture_output=True, text=True)
        # taskkill answers before the process is gone, and the next thing the
        # caller does is start nodes with the same names.
        time.sleep(1.0)
        return True

    found = _linux_pids(names)
    if not found:
        return False
    if note:
        print(note, flush=True)
    for pid, _ in found:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    for _ in range(20):
        if not _linux_alive(names):
            return True
        time.sleep(0.1)
    for pid, _ in _linux_pids(names):
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    time.sleep(0.2)
    return True


def spawn(target: Path, args, env: dict, log_dir: Path, log_name: str):
    """Start one node in the background, with a log file of its own.

    Five processes writing into one console is unreadable, and a launcher that
    exits while its nodes keep running needs their output somewhere that
    outlives it, so each node gets ``<log_dir>/<log_name>.log``.

    Windows: the child gets a process group and no console window of its own
    (``CREATE_NO_WINDOW``), so a node never scribbles on the console the
    launcher was started from. Linux: ``start_new_session`` takes the nodes out
    of the terminal's process group, so Ctrl+C on the launcher does not take
    the stack down with it.
    """
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / (log_name + ".log")
    handle = open(log_path, "wb")
    kwargs = {
        "cwd": str(REPO_ROOT),
        "env": env,
        "stdin": subprocess.DEVNULL,
        "stdout": handle,
        "stderr": subprocess.STDOUT,
    }
    if IS_WINDOWS:
        kwargs["creationflags"] = (subprocess.CREATE_NEW_PROCESS_GROUP
                                   | subprocess.CREATE_NO_WINDOW)
    else:
        kwargs["start_new_session"] = True
    proc = subprocess.Popen([str(target)] + [str(a) for a in args], **kwargs)
    return proc, log_path


def clear_logs(log_dir: Path) -> None:
    """Delete the previous run's node logs, retrying while a handle lingers.

    The launcher reads the lifecycle manager's log to learn when the stack is
    up, and a log left over from the previous run already ends in "Managed
    nodes are active" - so without this a second run decides the stack is ready
    the moment it looks. A process that has just been killed has not released
    its file yet, so the delete is retried rather than trusted.
    """
    marker = log_dir / "lifecycle_manager.log"
    for _ in range(10):
        if not marker.exists():
            return
        for path in log_dir.glob("*.log"):
            try:
                path.unlink()
            except OSError:
                pass
        time.sleep(1.0)


def tail(path: Path, limit: int = 20) -> str:
    """The last *limit* lines of a log, for an error report."""
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n".join(lines[-limit:])


def preset_params(argv, variable: str, default: str, token: str = "base_frame",
                  environ: dict | None = None) -> str:
    """The parameter preset a demo adds, unless the caller spoke for themselves.

    `slam_demo` and `amcl_demo` both preset the robot frame (upstream defaults
    to `base_footprint`, this simulator publishes `base_link`) and both have to
    hand the decision over as soon as the caller says anything about it - *token*
    anywhere in the arguments, or a `--params-file` that brings its own. Other
    parameters (`map_name`, `laser_max_range`, ...) do not get in the way: they
    are the common case, and dropping the preset for them used to leave the node
    looking for a frame that this simulator never publishes.

    `variable` (``NAV2_SLAM_PARAMS`` / ``NAV2_AMCL_PARAMS``) is the explicit
    override and always applies.
    """
    environ = os.environ if environ is None else environ
    if environ.get(variable):
        return environ[variable]
    for arg in argv:
        # `findstr /i` in the .bat twins: the frame test is case-insensitive
        # there, so it is here too - `Base_Frame_Id` has to withdraw the preset.
        if arg.lower() == "--params-file" or token.lower() in arg.lower():
            return ""
    return default


def wait_for_change_state(names, env: dict, steps: int = 20) -> bool:
    """Poll ``/<node>/change_state`` until every node answers.

    The lifecycle manager does this before it uses the service; a script that
    activates nodes by hand has to do it itself. Both nodes answer as soon as
    they are up, so one second per step is pacing, not patience.
    """
    for _ in range(steps):
        if all(ros2(["service", "type", "/%s/change_state" % name], env,
                    capture=True).returncode == 0 for name in names):
            return True
        time.sleep(1.0)
    return False


def configure_and_activate(names, env: dict, log_dir: Path, tag: str) -> int:
    """Drive configure(1) then activate(3) on each node, and say so.

    The transition ids are lifecycles_msgs' own, and the type is spelled the way
    `ros2 interface list` prints it - with the kind in it.
    """
    for name in names:
        for transition in (1, 3):
            done = ros2(["service", "call", "/%s/change_state" % name,
                         "lifecycle_msgs/srv/ChangeState",
                         "transition.id=%d" % transition], env, capture=True)
            if done.returncode != 0:
                print("[%s] could not drive /%s/change_state transition %d"
                      % (tag, name, transition), file=sys.stderr)
                print("[%s] see %s" % (tag, log_dir / (name + ".log")), file=sys.stderr)
                return 1
        print("[%s] %s is active" % (tag, name), flush=True)
    return 0


def run_foreground(target: Path, args, env: dict) -> int:
    """Run a node in the foreground and forward its exit code."""
    print("[launcher] %s %s" % (target.name, " ".join(str(a) for a in args)), flush=True)
    return subprocess.run([str(target)] + [str(a) for a in args],
                          cwd=str(REPO_ROOT), env=env).returncode
