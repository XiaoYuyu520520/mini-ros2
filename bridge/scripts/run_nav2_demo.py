#!/usr/bin/env python3
"""Run the nav2 end to end demo (``nav2_bridge_demo``) against the live bridge.

Examples
--------
    python bridge/scripts/run_nav2_demo.py --goal auto
    python bridge/scripts/run_nav2_demo.py --goal 5,-4 --report run.json
    python bridge/scripts/run_nav2_demo.py --plan-only
    python bridge/scripts/run_nav2_demo.py --build-only

What it does
------------
* builds ``nav2_bridge_demo`` when it is missing (or on ``--build``),
* exports the same environment ``bridge_env.bat`` sets: the shared memory slot
  geometry, the MSYS2 UCRT64 ``PATH`` the MinGW/gRPC runtime needs and the SDK
  config directory,
* makes sure the gRPC bridge daemon is running, because that is what mirrors
  ``/scan``, ``/odom`` and ``/map`` from the Ubuntu peer into shared memory,
* runs the demo and forwards its exit code (0 means the robot reached the goal).

Everything this script exports lives in this process tree only: nothing is
persisted, exactly like ``call bridge\\windows\\bridge_env.bat`` in a cmd window.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WINDOWS_ROOT = REPO_ROOT / "bridge" / "windows"
SHIM_ROOT = REPO_ROOT / "nav2" / "ros2_shim"
BUILD_DIR = SHIM_ROOT / "build"
DEMO_EXE = BUILD_DIR / "nav2_bridge_demo.exe"
DEFAULT_MSYS2 = Path("D:/WindowsServer2025_1/software/MSYS2")


def build_environment(msys2_root: Path) -> dict:
    """The environment bridge_env.bat sets, built in Python."""
    ucrt = msys2_root / "ucrt64" / "bin"
    env = os.environ.copy()
    if ucrt.is_dir():
        env["PATH"] = str(ucrt) + os.pathsep + env.get("PATH", "")
    env.setdefault("NAV2_SHM_SLOT_COUNT", "8")
    env.setdefault("NAV2_SHM_SLOT_SIZE", str(2 * 1024 * 1024))
    env.setdefault("NAV2_SHM_LARGE_SLOT_SIZE", str(8 * 1024 * 1024))
    env.setdefault("NAV2_SHM_GRAPH_DIR", str(Path(tempfile.gettempdir()) / "nav2_shim_graph"))
    env.setdefault("NAV2_SHM_LOG_LEVEL", "info")
    env.setdefault("ROS2_SHM_GRAPH_DIR", env["NAV2_SHM_GRAPH_DIR"])
    env.setdefault("ROS2_GRPC_SDK_CONFIG_DIR", str(REPO_ROOT / "gRPC" / "ros2_gRPC_sdk" / "config"))
    env.setdefault("ROS2_BRIDGE_CPU_ROOT", str(REPO_ROOT))
    return env


def build_demo(msys2_root: Path, force: bool) -> int:
    if DEMO_EXE.is_file() and not force:
        print(f"[run_nav2_demo] using existing {DEMO_EXE}")
        return 0
    ucrt = msys2_root / "ucrt64" / "bin"
    cmake = ucrt / "cmake.exe"
    if not cmake.is_file():
        print(f"[run_nav2_demo] missing {cmake}", file=sys.stderr)
        return 1
    env = build_environment(msys2_root)
    if not (BUILD_DIR / "CMakeCache.txt").is_file():
        print(f"[run_nav2_demo] configuring {BUILD_DIR}")
        code = subprocess.run(
            [str(cmake), "-S", str(SHIM_ROOT), "-B", str(BUILD_DIR), "-G", "Ninja",
             f"-DCMAKE_CXX_COMPILER={ucrt / 'g++.exe'}", "-DCMAKE_BUILD_TYPE=Release"],
            env=env).returncode
        if code != 0:
            return code
    print("[run_nav2_demo] building nav2_bridge_demo")
    return subprocess.run(
        [str(cmake), "--build", str(BUILD_DIR), "--target", "nav2_bridge_demo"],
        env=env).returncode


def daemon_running() -> bool:
    result = subprocess.run(
        ["tasklist", "/fi", "imagename eq ros2_bridge_daemon.exe"],
        capture_output=True, text=True)
    return "ros2_bridge_daemon.exe" in result.stdout


def ensure_daemon(msys2_root: Path) -> int:
    if daemon_running():
        print("[run_nav2_demo] bridge daemon already running")
        return 0
    script = WINDOWS_ROOT / "ros2_bridge.bat"
    print(f"[run_nav2_demo] starting the bridge daemon via {script.name}")
    code = subprocess.run(["cmd", "/c", str(script), "start"], cwd=str(WINDOWS_ROOT)).returncode
    if code != 0:
        return code
    # Give the daemon a moment to discover the agent and mirror its topics.
    time.sleep(2.5)
    print(f"[run_nav2_demo] daemon running: {daemon_running()}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", action="store_true", help="rebuild even if the exe exists")
    parser.add_argument("--build-only", action="store_true", help="build and exit")
    parser.add_argument("--msys2", default=str(DEFAULT_MSYS2))
    parser.add_argument("--no-daemon", action="store_true",
                        help="do not touch the bridge daemon")
    args, passthrough = parser.parse_known_args()

    msys2_root = Path(args.msys2)
    code = build_demo(msys2_root, args.build or args.build_only)
    if code != 0 or args.build_only:
        return code

    if not args.no_daemon:
        code = ensure_daemon(msys2_root)
        if code != 0:
            return code

    if not passthrough:
        passthrough = ["--goal", "auto"]
    print("[run_nav2_demo] " + " ".join([DEMO_EXE.name] + passthrough))
    sys.stdout.flush()
    return subprocess.run(
        [str(DEMO_EXE)] + passthrough, cwd=str(REPO_ROOT),
        env=build_environment(msys2_root)).returncode


if __name__ == "__main__":
    sys.exit(main())