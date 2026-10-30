# -*- coding: utf-8 -*-
"""Catch the `shm_transport` stall under load, with a cap far above the test's own.

The test has an internal `WaitForSingleObject(..., 40000)` (test_shm.cpp:378-379),
so a 30 s cap cannot tell "stalled for forty seconds" from "stuck forever". The
`ctest -j 8` round-9 instance is already known to be the latter - `test_shm.exe`
was alive for 813 s with 1.11 s of CPU - but a cheap on-demand reproducer is
worth having, so this runs it alone, in its own namespace, on the same six cores
as everything else, with three busy Python threads taking up the slack.

    python .scratch/_shm_transport_under_load.py [attempts] [cap]
"""
from __future__ import annotations

import ctypes
import os
import subprocess
import sys
import threading
import time

CORES = [0, 1, 10, 11, 12, 13]
UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
BUILD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build"
NAMESPACE = "probe_shm_loaded"


def pin_self() -> int:
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = ctypes.c_void_p
    k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    mask = 0
    for cpu in CORES:
        mask |= 1 << cpu
    k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), mask)
    return mask


def main() -> int:
    attempts = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    cap = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    mask = pin_self()

    stop = threading.Event()

    def ballast() -> None:
        # Keep the six P-cores busy so a thread that has to be scheduled to make
        # progress against another one is not always scheduled immediately.
        while not stop.is_set():
            x = 0
            for _ in range(200000):
                x += 1

    threads = [threading.Thread(target=ballast, daemon=True) for _ in range(3)]
    for t in threads:
        t.start()

    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    env["NAV2_SHM_NAMESPACE"] = NAMESPACE
    argv = [os.path.join(UCRT, "ctest.exe"), "--test-dir", BUILD, "-R", "^shm_transport$",
            "--timeout", str(cap), "-V"]
    print("shm_transport, isolated namespace, 3 ballast threads, %d s cap, mask 0x%X"
          % (cap, mask), flush=True)

    try:
        for attempt in range(1, attempts + 1):
            began = time.monotonic()
            done = subprocess.run(argv, env=env, capture_output=True, text=True,
                                  errors="replace", timeout=cap * 3 + 60)
            elapsed = time.monotonic() - began
            text = done.stdout + done.stderr
            ok = done.returncode == 0
            print("[%2d] %6.1fs rc=%d %s" % (attempt, elapsed, done.returncode,
                                             "ok" if ok else "NOT OK"), flush=True)
            if not ok:
                print("---- last 30 lines ----")
                for l in text.splitlines()[-30:]:
                    print("  " + l.rstrip())
                return 1
    finally:
        stop.set()
    return 0


if __name__ == "__main__":
    sys.exit(main())
