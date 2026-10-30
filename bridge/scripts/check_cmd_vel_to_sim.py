#!/usr/bin/env python3
"""The other direction: Windows -> relay -> simulator.

Publishing on /cmd_vel from the Windows side has to travel
daemon -> gRPC -> relay -> simulator agent -> the car, and the car has to
answer by moving, i.e. its /odom pose has to change.

    python bridge/scripts/check_cmd_vel_to_sim.py --host 192.168.0.153
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
DEFAULT_KEY = Path(r"C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _bridge_env import bridge_env  # noqa: E402

# Reads one /odom off the simulator agent and prints the pose as "x y".
ODOM_PROBE = r"""cd ~/code/CPU
PYTHONPATH=bridge/ubuntu .venv/bin/python - <<'EOS'
import sys, grpc
sys.path.insert(0, 'bridge/ubuntu')
from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb
from ros2_bridge_sim.proto import ros2_bridge_pb2_grpc as pb_grpc
from ros2_bridge_sim import messages
stub = pb_grpc.Ros2BridgeStub(grpc.insecure_channel('127.0.0.1:50051'))
for m in stub.Subscribe(pb.SubscribeRequest(topic='/odom')):
    d = messages.decode(m.data, m.type)
    p = d['pose']['pose']['position']
    print('POSE %.6f %.6f' % (p['x'], p['y']))
    break
EOS
"""


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], **kw)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--port", type=int, default=50060)
    parser.add_argument("--user", default="wang")
    parser.add_argument("--ssh-key", default=str(DEFAULT_KEY))
    parser.add_argument("--speed", type=float, default=0.4)
    parser.add_argument("--seconds", type=float, default=3.0)
    args = parser.parse_args()

    daemon = BRIDGE / "ros2_bridge_daemon.exe"
    cli = BRIDGE / "ros2.exe"
    if not daemon.is_file():
        print("daemon not built: %s" % daemon, file=sys.stderr)
        return 1

    target = "%s:%d" % (args.host, args.port)
    # The daemon sizes its shared-memory channels from NAV2_SHM_*, so it has
    # to be started with the bridge environment, not the bare one this script
    # inherited.
    env = bridge_env()
    env["ROS2_BRIDGE_AGENT"] = target

    def ssh_cmd(script: str, timeout: int = 30) -> str:
        r = subprocess.run(
            ["ssh", "-i", args.ssh_key, "-o", "BatchMode=yes",
             "%s@%s" % (args.user, args.host), "bash -s"],
            input=script.encode(), capture_output=True, timeout=timeout)
        return r.stdout.decode("utf-8", "replace")

    def pose() -> tuple[float, float] | None:
        out = ssh_cmd(ODOM_PROBE)
        for line in out.splitlines():
            if line.startswith("POSE "):
                _, x, y = line.split()
                return float(x), float(y)
        return None

    run(["taskkill", "/F", "/IM", "ros2_bridge_daemon.exe"], capture_output=True)
    run(["taskkill", "/F", "/IM", "ros2.exe"], capture_output=True)
    time.sleep(1)

    print("=== starting the daemon against %s ===" % target)
    proc = subprocess.Popen([str(daemon), "--agent", target],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    publisher = None
    try:
        time.sleep(6)

        before = pose()
        print("pose before : %s" % (before,))

        print("=== publishing /cmd_vel %.2f for %.1fs from Windows ===" % (args.speed, args.seconds))
        # `topic pub` keeps publishing until told to stop, which is what a
        # real controller does; it is stopped by killing it below.
        publisher = subprocess.Popen(
            [str(cli), "topic", "pub", "/cmd_vel", "geometry_msgs/msg/Twist",
             "{linear: {x: %s}}" % args.speed, "-r", "10"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        time.sleep(args.seconds)
        publisher.terminate()
        try:
            publisher.wait(timeout=5)
        except subprocess.TimeoutExpired:
            publisher.kill()
        publisher = None
        time.sleep(2.5)

        after = pose()
        print("pose after  : %s" % (after,))

        if before is None or after is None:
            print("could not read the simulator pose", file=sys.stderr)
            return 1
        moved = ((after[0] - before[0]) ** 2 + (after[1] - before[1]) ** 2) ** 0.5
        print("moved       : %.3f m" % moved)
        if moved < 0.02:
            print("RESULT: FAIL - the car did not move", file=sys.stderr)
            return 1
        print("RESULT: PASS - a command published from Windows moved the car")
        return 0
    finally:
        if publisher is not None:
            publisher.kill()
        run(["taskkill", "/F", "/IM", "ros2.exe"], capture_output=True)
        run(["taskkill", "/F", "/IM", "ros2_bridge_daemon.exe"], capture_output=True)
        if proc.poll() is None:
            proc.terminate()


if __name__ == "__main__":
    sys.exit(main())