#!/usr/bin/env python3
"""Push slices of this repo to the Ubuntu box, keeping the tree shape.

    python .scratch/linux_push.py tools nav2/ros2_shim
    python .scratch/linux_push.py tools --remote-file .scratch/remote_stage1.sh

Directories are tarred from the repo root (so `nav2/ros2_shim` lands as
`~/code/CPU/nav2/ros2_shim`), build trees and __pycache__ are skipped, and the
tarball is streamed straight into `tar -xzf -`. Nothing is written to disk here,
which matters when the slice is a few hundred megabytes.
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
KEY = os.path.expanduser(r"~/Desktop/app/ssh_key/id_ed25519")
TARGET = "ubuntu@192.168.1.113"
REMOTE_ROOT = "~/code/CPU"
SKIP_DIRS = {"build", "__pycache__", ".git", ".scratch", ".venv", "node_modules"}


def collect(paths: list[str]) -> bytes:
    buf = io.BytesIO()
    count = 0
    with tarfile.open(fileobj=buf, mode="w:gz", compresslevel=1) as tar:
        for rel in paths:
            base = REPO_ROOT / rel
            if not base.exists():
                raise SystemExit("no such path: %s" % base)
            if base.is_file():
                tar.add(str(base), arcname=rel)
                count += 1
                continue
            for path in sorted(base.rglob("*")):
                sub = path.relative_to(base)
                if SKIP_DIRS & set(sub.parts) or path.is_dir():
                    continue
                tar.add(str(path), arcname="%s/%s" % (rel, sub.as_posix()))
                count += 1
    print("[push] %d files" % count, flush=True)
    return buf.getvalue()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--remote-cmd", help="shell command to run after unpacking")
    ap.add_argument("--remote-file", help="read the remote shell script from here")
    ap.add_argument("--timeout", type=float, default=1800.0)
    args = ap.parse_args()

    began = time.monotonic()
    data = collect(args.paths)
    print("[push] %s -> %d bytes (tarred in %.1fs)"
          % (" ".join(args.paths), len(data), time.monotonic() - began), flush=True)

    if args.remote_file:
        args.remote_cmd = pathlib.Path(args.remote_file).read_text(encoding="utf-8")
    remote = "mkdir -p %s && tar -xzf - -C %s && echo unpacked" % (REMOTE_ROOT, REMOTE_ROOT)
    payload = data
    if args.remote_cmd:
        # The script cannot ride behind the tarball on the same stdin: tar reads
        # the pipe to EOF, so it would see the shell script as trailing garbage.
        # It goes as base64 in the command line instead, into a file, and runs
        # only after the unpack succeeded.
        blob = base64.b64encode(args.remote_cmd.encode("utf-8")).decode("ascii")
        remote += (" && echo %s | base64 -d > /tmp/push_cmd.sh"
                   " && bash /tmp/push_cmd.sh" % blob)
    argv = ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "BatchMode=yes",
            "-i", KEY, TARGET, remote]
    began = time.monotonic()
    try:
        done = subprocess.run(argv, input=payload, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print("[push] timed out after %gs" % args.timeout, file=sys.stderr)
        return 124
    print("[push] ssh rc=%d in %.1fs" % (done.returncode, time.monotonic() - began), flush=True)
    return done.returncode


if __name__ == "__main__":
    sys.exit(main())