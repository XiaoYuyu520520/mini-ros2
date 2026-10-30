# -*- coding: utf-8 -*-
"""Copy this checkout's racecar workspace to the Ubuntu machine and list its packages.

Same shape as bridge/scripts/sync_to_ubuntu.py (tar the tree, unpack over there)
but for code/Desktop/racecar, which is what has to be *built* on Linux to prove
the package is not Windows-only. Build products never go over.

    python .scratch/push_racecar.py [--build]
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tarfile

ROOT = r"E:\DesktopData\app\code\CPU"
LOCAL = os.path.join(ROOT, "code", "Desktop", "racecar")
ARCHIVE = os.path.join(ROOT, ".scratch", "racecar_push.tar.gz")
KEY = r"C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519"
TARGET = "ubuntu@192.168.1.113"
REMOTE = "~/code/CPU/code/Desktop/racecar"
# Only the *top level* build/install/log are colcon's; a directory named "log"
# anywhere deeper is source (openslam_gmapping keeps its log module in
# include/gmapping/log/) and skipping those is how a tree ends up unable to
# compile - see bridge/docs/04-gaps.md section 28 item 17.
TOP_LEVEL_SKIP = {"build", "install", "log"}
ANY_LEVEL_SKIP = {"__pycache__", ".git", ".pytest_cache"}


def ssh(command, timeout=600):
    print("+ ssh " + command)
    return subprocess.run(["ssh", "-i", KEY, "-o", "StrictHostKeyChecking=no",
                           "-o", "BatchMode=yes", TARGET, command],
                          timeout=timeout).returncode


def pack():
    def skip(info):
        parts = info.name.split("/")
        if parts[0] in TOP_LEVEL_SKIP:
            return None
        if any(part in ANY_LEVEL_SKIP for part in parts):
            return None
        return info

    count = 0
    with tarfile.open(ARCHIVE, "w:gz") as handle:
        for entry in sorted(os.listdir(LOCAL)):
            handle.add(os.path.join(LOCAL, entry), arcname=entry, filter=skip)
            count += 1
    print("packed %d top level entries -> %s (%.1f MB)"
          % (count, ARCHIVE, os.path.getsize(ARCHIVE) / 1048576.0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", action="store_true", help="colcon build after unpacking")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()

    pack()
    code = subprocess.run(["scp", "-i", KEY, "-o", "StrictHostKeyChecking=no",
                           "-o", "BatchMode=yes", ARCHIVE, TARGET + ":/tmp/racecar_push.tar.gz"],
                          timeout=900).returncode
    if code != 0:
        return code
    unpack = ("mkdir -p ~/code/CPU/code/Desktop/racecar && "
              "tar -xzf /tmp/racecar_push.tar.gz -C ~/code/CPU/code/Desktop/racecar && "
              "rm -f /tmp/racecar_push.tar.gz && "
              "echo '--- unpacked ---' && ls ~/code/CPU/code/Desktop/racecar && "
              "echo '--- colcon list ---' && "
              "source /opt/ros/jazzy/setup.bash && cd ~/code/CPU/code/Desktop/racecar && "
              "colcon list 2>&1 | head -20")
    if ssh(unpack) != 0:
        return 1
    if args.build:
        build = ("source /opt/ros/jazzy/setup.bash && cd %s && "
                 "colcon build --parallel-workers %d 2>&1 | tail -40" % (REMOTE, args.jobs))
        return ssh(build, timeout=1800)
    return 0


if __name__ == "__main__":
    sys.exit(main())
