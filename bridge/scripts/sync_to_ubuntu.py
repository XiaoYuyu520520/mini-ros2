#!/usr/bin/env python3
"""Deploy the Ubuntu side of the bridge to the robot/simulation machine.

Copies ``bridge/ubuntu/ros2_bridge_sim`` to the remote host and (optionally)
refreshes its virtualenv. The remote never needs a ROS2 installation: the
simulator generates laser scans, odometry, TF and the map itself and serves
them over gRPC, which is exactly what this bridge is for.

    python bridge/scripts/sync_to_ubuntu.py
    python bridge/scripts/sync_to_ubuntu.py --run          # sync then start the sim
    python bridge/scripts/sync_to_ubuntu.py --host 10.0.0.5 --user pi

Defaults come from the task description: ubuntu@192.168.1.113 with the key at
C:\\Users\\Administrator\\Desktop\\app\\ssh_key\\id_ed25519.
"""
from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LOCAL_SIM = REPO_ROOT / "bridge" / "ubuntu" / "ros2_bridge_sim"
DEFAULT_KEY = Path(r"C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519")


def run(cmd: list[str], **kwargs) -> int:
    print("+ " + " ".join(shlex.quote(str(part)) for part in cmd), flush=True)
    return subprocess.run([str(part) for part in cmd], **kwargs).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="192.168.1.113")
    parser.add_argument("--user", default="ubuntu")
    parser.add_argument("--port", default="22")
    parser.add_argument("--key", default=str(DEFAULT_KEY))
    parser.add_argument("--remote-root", default="~/code/CPU")
    parser.add_argument("--no-venv", action="store_true",
                        help="skip creating/refreshing the remote virtualenv")
    parser.add_argument("--run", action="store_true", help="start the simulator afterwards")
    parser.add_argument("--agent-port", default="50051")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not LOCAL_SIM.is_dir():
        raise SystemExit(f"missing {LOCAL_SIM}")
    if not Path(args.key).is_file():
        raise SystemExit(f"missing ssh key {args.key}")

    target = f"{args.user}@{args.host}"
    ssh_base = ["ssh", "-i", args.key, "-p", args.port,
                "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new"]
    remote_root = args.remote_root

    if args.dry_run:
        print(f"would copy {LOCAL_SIM} -> {target}:{remote_root}/bridge/ubuntu/ros2_bridge_sim")
        return 0

    # The archive holds the *contents* of ros2_bridge_sim, so it has to be
    # unpacked into the package directory itself. Unpacking one level up (into
    # bridge/ubuntu) scattered the package's modules next to it, and the
    # simulator then kept importing the copy that was already there.
    remote_pkg = f"{remote_root}/bridge/ubuntu/ros2_bridge_sim"
    run(ssh_base + [target, f"mkdir -p {remote_pkg}"])

    # tar over ssh keeps permissions and is one round trip.
    remote_extract = f"tar -xzf - -C {remote_pkg}"
    print(f"+ tar -czf - -C {LOCAL_SIM} . | ssh {target} '{remote_extract}'")
    producer = subprocess.Popen(
        ["tar", "-czf", "-", "-C", str(LOCAL_SIM), "."],
        stdout=subprocess.PIPE)
    extractor = subprocess.Popen(
        ssh_base + [target.replace("'", ""), remote_extract],
        stdin=producer.stdout)
    producer.stdout.close()
    producer.wait()
    code = extractor.wait()
    if code != 0:
        return code

    if not args.no_venv:
        setup = (
            "set -e; cd {root}; "
            "[ -d .venv ] || python3 -m venv .venv; "
            ".venv/bin/python -m pip install -q --upgrade pip; "
            ".venv/bin/python -m pip install -q -r bridge/ubuntu/ros2_bridge_sim/requirements.txt"
        ).format(root=remote_root)
        code = run(ssh_base + [target, setup])
        if code != 0:
            return code

    print(f"synced to {target}:{remote_root}/bridge/ubuntu/ros2_bridge_sim")

    if args.run:
        launch = (
            "cd {root}/bridge/ubuntu && exec .venv/bin/python -m ros2_bridge_sim.run_sim "
            "--port {port}"
        ).format(root=remote_root, port=args.agent_port)
        print(f"+ ssh {target} '{launch}'   (Ctrl+C to stop)")
        return run(ssh_base + [target, launch], cwd=None)
    return 0


if __name__ == "__main__":
    sys.exit(main())