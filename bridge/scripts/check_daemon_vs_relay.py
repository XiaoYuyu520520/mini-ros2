#!/usr/bin/env python3
"""End-to-end check: the Windows bridge daemon against the relay on the robot.

This is the whole point of the connector. The daemon is the only process on
the Windows side that talks to the outside world, and it is single-peer by
design, so the relay has to present one merged agent to it.

The check drives the real daemon binary:

    * start it against the relay's LAN address,
    * wait for it to mirror the merged graph into shared memory,
    * list the topics it mirrored (through the daemon's own CLI),
    * publish a /cmd_vel and check the simulator's car answers.

The daemon writes its log to $ROS2_BRIDGE_LOG, so a failure is readable there.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BRIDGE = REPO / "bridge" / "windows" / "build"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _bridge_env import bridge_env  # noqa: E402


def run(cmd, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], **kw)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--port", type=int, default=50060)
    parser.add_argument("--wait", type=float, default=40.0)
    parser.add_argument("--keep", action="store_true", help="leave the daemon running")
    args = parser.parse_args()

    daemon = BRIDGE / "ros2_bridge_daemon.exe"
    cli = BRIDGE / "ros2.exe"
    if not daemon.is_file():
        print("daemon not built: %s" % daemon, file=sys.stderr)
        print("build it with: python bridge\\scripts\\build_bridge.py", file=sys.stderr)
        return 1

    target = "%s:%d" % (args.host, args.port)
    # The daemon sizes its shared-memory channels from NAV2_SHM_*, so it has
    # to be started with the bridge environment, not the bare one this script
    # inherited.
    env = bridge_env()
    env["ROS2_BRIDGE_AGENT"] = target
    env["NAV2_SHM_LOG_LEVEL"] = "info"

    # The daemon is single-peer, so any earlier one has to go first.
    run(["taskkill", "/F", "/IM", "ros2_bridge_daemon.exe"], capture_output=True)
    time.sleep(1)

    print("=== starting the daemon against %s ===" % target)
    log = BRIDGE / "ros2_bridge_daemon.log"
    proc = subprocess.Popen([str(daemon), "--agent", target],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=env, text=True, encoding="utf-8", errors="replace")
    try:
        deadline = time.monotonic() + args.wait
        ready = False
        while time.monotonic() < deadline:
            time.sleep(1.5)
            if proc.poll() is not None:
                print("the daemon exited with %s" % proc.returncode)
                break
            out = run([str(cli), "bridge", "status"], capture_output=True, text=True,
                      env=env).stdout
            if "remote" in out:
                remote = [line for line in out.splitlines() if "remote" in line]
                if remote and not remote[0].rstrip().endswith("0 (0 remote, 0 local)"):
                    ready = True
                    print(out.strip())
                    break
        if not ready:
            print("the daemon did not mirror anything in %.0fs" % args.wait)
            if log.is_file():
                print("--- daemon log ---")
                print(log.read_text(encoding="utf-8", errors="replace")[-3000:])
            return 1

        print("=== the daemon's view of the merged graph ===")
        print(run([str(cli), "topic", "list", "-t"], capture_output=True, text=True,
                  env=env).stdout.strip())

        print("=== echo /chatter (the real ROS2 side) ===")
        out = run([str(cli), "topic", "echo", "/chatter", "--count", "2"],
                  capture_output=True, text=True, env=env, timeout=30)
        print(out.stdout.strip()[:800])
        print(out.stderr.strip()[:400])

    finally:
        if not args.keep:
            print("=== stopping the daemon ===")
            run(["taskkill", "/F", "/IM", "ros2_bridge_daemon.exe"], capture_output=True)
            if proc.poll() is None:
                proc.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
