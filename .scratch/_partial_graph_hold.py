# -*- coding: utf-8 -*-
"""Reproduce the `ctest -j 8` round-3 signature on purpose.

Round 3 failed exactly one check - `test_slam_mapping.cpp:177`, the subscriber
count of /scan - while the same run still produced 237 /map messages, so every
other assertion passed.

The sequence inside the test is: the driver registers its /scan publisher (137),
its /map subscription (141) and its `pose` subscription (155), and *then*
`slam->activate()` (172) registers the slam node's own /scan subscription. The
check at :177 reads the merged graph. If the file write that carries that last
registration is refused, the /scan topic line still says `subscribers=0` while
/map and pose are already in the file - which is the round-3 shape.

So: let the first writes land, wait until the file holds /map and pose, and only
then take the file away from the writer.

    python .scratch/_partial_graph_hold.py
"""
from __future__ import annotations

import os
import subprocess
import sys
import time

UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
EXE = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\slam_toolbox_port\test_slam_mapping.exe"
CWD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build"
NAMESPACE = "probe_partial_hold"


def main() -> int:
    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    env["NAV2_SHM_NAMESPACE"] = NAMESPACE
    env["NAV2_SHM_LOG_LEVEL"] = "info"

    directory = os.path.join(env.get("TEMP") or ".", "nav2_shim_graph_" + NAMESPACE)
    os.makedirs(directory, exist_ok=True)
    for name in os.listdir(directory):
        try:
            os.remove(os.path.join(directory, name))
        except OSError:
            pass

    started = time.time()
    proc = subprocess.Popen(
        [EXE], cwd=CWD, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace")

    handle = None
    note = "never saw /map and pose together"
    path = None
    while time.time() - started < 20.0:
        names = [n for n in os.listdir(directory) if n.endswith(".graph")]
        if names:
            path = os.path.join(directory, names[0])
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as probe:
                    content = probe.read()
            except OSError:
                content = ""
            if "/map" in content and "pose" in content:
                break
        time.sleep(0.0005)

    if path is not None:
        try:
            handle = open(path, "rb")
            note = "held %s from t=%.3fs for 2.0s" % (os.path.basename(path), time.time() - started)
        except OSError as exc:
            note = "could not open: %s" % exc
        time.sleep(2.0)

    out, _ = proc.communicate(timeout=300)
    if handle is not None:
        handle.close()

    print("rc=%d, wall=%.1fs, %s" % (proc.returncode, time.time() - started, note))
    for line in out.splitlines():
        s = line.strip()
        if "FAIL" in s or "check(s) failed" in s or "all checks passed" in s \
                or "map messages" in s or "pose messages" in s:
            print("  | " + s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
