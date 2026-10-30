# -*- coding: utf-8 -*-
"""Prove (or kill) the "a lost graph write is what zeroes the subscriber count" theory.

`test_slam_mapping.cpp:177` asserts `scan_pub->get_subscription_count() > 0`
right after the slam node activates, and fails that one check under `ctest -j 8`
while everything else in the same run passes. It is the process's *first* graph
query, so the 250 ms cache in `merged_topics_cached()` cannot be the cause: the
first call always refreshes.

What is left is the file. `collect()` (graph.hpp:789) reads only the files in the
graph directory, `read_shared` gives up after 60 ms (graph.hpp:248), and
`collect()` silently skips a file it could not open (graph.hpp:818-820) - so when
the writer is mid-swap, the node's own advertisements vanish from the node's own
view. On the writer's side `flush_locked()` (graph.hpp:723) ignores the return
value of `install_file()`, so a refused swap is lost until the next change.

This experiment makes the swap deterministic: hold `<pid>.graph` open the way an
idxer, an antivirus or `std::ifstream` in an older build does - read + write
share, no delete share - so the writer's `MoveFileExW`/`ReplaceFileW` cannot
replace it. If the check at :177 is the only thing that fails, the theory holds.

    python .scratch/_greedy_graph_hold.py            # control, then the hold
"""
from __future__ import annotations

import io
import os
import subprocess
import sys
import time

UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
EXE = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\slam_toolbox_port\test_slam_mapping.exe"
CWD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build"


def run(namespace: str, hold_seconds: float) -> tuple[int, str, str]:
    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    env["NAV2_SHM_NAMESPACE"] = namespace
    env["NAV2_SHM_LOG_LEVEL"] = "info"

    directory = os.path.join(env.get("TEMP") or env.get("TMP") or ".", "nav2_shim_graph_" + namespace)
    if os.path.isdir(directory):
        for name in os.listdir(directory):
            try:
                os.remove(os.path.join(directory, name))
            except OSError:
                pass
    else:
        os.makedirs(directory, exist_ok=True)

    started = time.time()
    proc = subprocess.Popen(
        [EXE], cwd=CWD, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace")

    held = "not held"
    handle = None
    if hold_seconds > 0.0:
        path = None
        while time.time() - started < 20.0:
            names = [n for n in os.listdir(directory) if n.endswith(".graph")]
            if names:
                path = os.path.join(directory, names[0])
                break
            time.sleep(0.001)
        if path is None:
            held = "no graph file ever appeared"
        else:
            appeared = time.time() - started
            try:
                # Python's open() asks for read+write share and never shares
                # delete, which is exactly the holder that blocks the swap.
                handle = open(path, "rb")
                held = "held %s at t=%.3fs for %.1fs" % (os.path.basename(path), appeared, hold_seconds)
            except OSError as exc:
                held = "could not open %s: %s" % (path, exc)
            time.sleep(hold_seconds)

    out, _ = proc.communicate(timeout=300)
    if handle is not None:
        handle.close()
    return proc.returncode, held, out


def main() -> int:
    for label, hold in (("control (nothing held)", 0.0), ("hold the graph file", 6.0)):
        print("=" * 72)
        print(label)
        started = time.time()
        rc, held, out = run("probe_greedy_%d" % int(hold), hold)
        print("  rc=%d, wall=%.1fs, %s" % (rc, time.time() - started, held))
        for line in out.splitlines():
            if "FAIL" in line or "check(s) failed" in line or "all checks passed" in line \
                    or "map messages" in line or "pose messages" in line:
                print("  | " + line.strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
