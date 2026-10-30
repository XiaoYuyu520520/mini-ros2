#!/usr/bin/env python3
"""Install the Ubuntu-side dependencies with a fast mirror, detached.

The plain `pip install` on the robot pulls the wheels from pypi.org and took
longer than the caller's patience. The Tsinghua mirror serves the same
wheels and is reachable from that network, so the install is started against
it and then polled.

    python bridge/scripts/install_ubuntu_deps.py --mirror
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_KEY = Path(r"C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519")
TSINGHUA = "https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple"


def ssh(target, key, port, script, timeout=30):
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
    parser.add_argument("--mirror", action="store_true", help="use the Tsinghua mirror")
    parser.add_argument("--wait", type=int, default=240)
    args = parser.parse_args()

    key = str(args.key)
    target = f"{args.user}@{args.host}"
    root = args.remote_root
    index = f" -i {TSINGHUA}" if args.mirror else ""

    # Anything still running from an earlier attempt would hold the pip lock
    # and make this one wait for it, so it is cleared first.
    start = "\n".join([
        "pkill -f 'pip install' 2>/dev/null || true",
        "sleep 1",
        f"cd {root}",
        "mkdir -p ~/.ros2_link",
        "cat > /tmp/install_deps.sh <<'EOS'",
        "set -e",
        f"cd {root}",
        "[ -d .venv ] || python3 -m venv .venv",
        f".venv/bin/python -m pip install -q --upgrade pip{index}",
        f".venv/bin/python -m pip install{index} -r bridge/ubuntu/ros2_bridge_sim/requirements.txt",
        "echo INSTALL_DONE",
        "EOS",
        "setsid nohup bash /tmp/install_deps.sh > ~/.ros2_link/install.log 2>&1 < /dev/null &",
        "echo started",
    ]) + "\n"

    r = ssh(target, key, args.port, start, timeout=40)
    print(r.stdout.decode("utf-8", "replace").strip())
    if r.returncode != 0:
        print("STDERR:", r.stderr.decode("utf-8", "replace")[:400], file=sys.stderr)
        return r.returncode

    deadline = time.monotonic() + args.wait
    while time.monotonic() < deadline:
        time.sleep(5)
        r = ssh(target, key, args.port,
                "tail -n 2 ~/.ros2_link/install.log 2>/dev/null; "
                "grep -c INSTALL_DONE ~/.ros2_link/install.log 2>/dev/null || true\n",
                timeout=30)
        out = r.stdout.decode("utf-8", "replace")
        if "INSTALL_DONE" in out:
            print("install finished")
            return 0
    print("install still running - check ~/.ros2_link/install.log")
    return 1


if __name__ == "__main__":
    sys.exit(main())
