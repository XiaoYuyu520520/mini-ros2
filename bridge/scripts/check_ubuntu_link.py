#!/usr/bin/env python3
"""Drive the Ubuntu side of the connector end to end.

Starts the simulator agent and the relay on the robot, then reports what
each of them advertises. Used to check the relay without a Windows machine
in the loop.

    python bridge/scripts/check_ubuntu_link.py
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_KEY = Path(r"C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519")


def ssh(target, key, port, script, timeout=60):
    return subprocess.run(
        ["ssh", "-i", key, "-p", port, "-o", "BatchMode=yes",
         "-o", "StrictHostKeyChecking=accept-new", target, "bash -s"],
        input=script.encode("utf-8"), capture_output=True, timeout=timeout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.153")
    parser.add_argument("--user", default="wang")
    parser.add_argument("--port", default="22")
    parser.add_argument("--key", default=str(DEFAULT_KEY))
    parser.add_argument("--remote-root", default="~/code/CPU")
    parser.add_argument("--sim-port", default="50051")
    parser.add_argument("--link-port", default="50060")
    args = parser.parse_args()

    key = str(args.key)
    target = f"{args.user}@{args.host}"
    root = args.remote_root

    script = "\n".join([
        f"cd {root}",
        "echo '=== start the simulator agent ==='",
        "pkill -f ros2_bridge_sim.run_sim 2>/dev/null || true",
        "sleep 1",
        "cd bridge/ubuntu",
        "PYTHONPATH=. setsid nohup ../../.venv/bin/python -m ros2_bridge_sim.run_sim "
        f"--no-console --host 0.0.0.0 --port {args.sim_port} "
        "> ~/sim.log 2>&1 < /dev/null &",
        "sleep 3",
        "echo '--- sim.log ---'",
        "tail -n 5 ~/sim.log",
        "echo '--- listening ---'",
        f"ss -lnt 2>/dev/null | grep ':{args.sim_port} ' || echo 'NOT LISTENING'",
        "echo '=== start the relay ==='",
        "pkill -f ros2_link.relay 2>/dev/null || true",
        "sleep 1",
        "PYTHONPATH=. setsid nohup ../../.venv/bin/python -m ros2_link.relay "
        f"--sim 127.0.0.1:{args.sim_port} --real 127.0.0.1:50111 "
        f"--listen 0.0.0.0:{args.link_port} --loopback-listen 127.0.0.1:{args.link_port} "
        "> ~/relay.log 2>&1 < /dev/null &",
        "sleep 4",
        "echo '--- relay.log ---'",
        "tail -n 20 ~/relay.log",
        "echo '--- listening ---'",
        f"ss -lnt 2>/dev/null | grep ':{args.link_port} ' || echo 'NOT LISTENING'",
    ]) + "\n"

    r = ssh(target, key, args.port, script, timeout=120)
    print(r.stdout.decode("utf-8", "replace"))
    e = r.stderr.decode("utf-8", "replace")
    if e.strip():
        print("STDERR:", e[:600], file=sys.stderr)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
