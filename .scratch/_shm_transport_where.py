# -*- coding: utf-8 -*-
"""Where does `shm_transport` hang, and does the live daemon have to be there?

`ctest -j 8` round 9 hung on it for 900 s. The same test, run alone and serial
with a 30 s cap, hangs roughly once in fourteen attempts - so it is an on-demand
reproducer, not a scheduling artifact. Two questions left:

  * what is the test doing when it hangs (its own stdout, `-V`);
  * does it need the default namespace - the shared-memory channels and the graph
    directory it shares with the live `ros2_bridge_daemon` - or does it deadlock
    on its own.

    python .scratch/_shm_transport_where.py shared 30
    python .scratch/_shm_transport_where.py isolated 30
"""
from __future__ import annotations

import ctypes
import os
import subprocess
import sys
import time

CORES = [0, 1, 10, 11, 12, 13]
UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
BUILD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build"


def pin() -> int:
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = ctypes.c_void_p
    k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    mask = 0
    for cpu in CORES:
        mask |= 1 << cpu
    k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), mask)
    return mask


def main() -> int:
    mode = sys.argv[1] if len(sys.argv) > 1 else "shared"
    attempts = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    cap = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    mask = pin()

    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    if mode == "isolated":
        env["NAV2_SHM_NAMESPACE"] = "probe_shm_isolated"
    print("shm_transport, mode=%s, up to %d attempts, %d s cap, mask 0x%X"
          % (mode, attempts, cap, mask), flush=True)

    argv = [os.path.join(UCRT, "ctest.exe"), "--test-dir", BUILD,
            "-R", "^shm_transport$", "--timeout", str(cap), "-V"]

    for attempt in range(1, attempts + 1):
        began = time.monotonic()
        try:
            done = subprocess.run(argv, env=env, capture_output=True, text=True,
                                  errors="replace", timeout=cap * 3 + 60)
            text = done.stdout + done.stderr
            rc = done.returncode
        except subprocess.TimeoutExpired:
            print("[%2d] ctest itself had to be killed" % attempt, flush=True)
            return 2
        elapsed = time.monotonic() - began
        hung = "Timeout" in text or rc != 0
        print("[%2d] %5.1fs rc=%d %s" % (attempt, elapsed, rc, "HUNG" if hung else "ok"),
              flush=True)
        if hung:
            lines = [l.rstrip() for l in text.splitlines()]
            print("---- last 40 lines of the hung attempt ----")
            for l in lines[-40:]:
                print("  " + l)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
