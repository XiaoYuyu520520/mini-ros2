# -*- coding: utf-8 -*-
"""Locate the `shm_transport` hang: run test_shm.exe on a console, under load.

`ctest -V` shows nothing from a hung run because the test's stdout is a pipe and
the C runtime block-buffers it - a passing run does not flush either, so that
output is useless for locating the hang. Handing the child the console directly
makes its `printf`s line buffered, so the last line before the stall is the line
the test was on.

    python .scratch/_shm_transport_locate.py [attempts]
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
EXE = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\shm_transport\test_shm.exe"
CWD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\shm_transport"
NAMESPACE = "probe_shm_locate"
STALL_SECONDS = 25.0


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
    attempts = int(sys.argv[1]) if len(sys.argv) > 1 else 25
    mask = pin_self()
    stop = threading.Event()

    def ballast() -> None:
        while not stop.is_set():
            x = 0
            for _ in range(200000):
                x += 1

    ballast_threads = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    for _ in range(ballast_threads):
        threading.Thread(target=ballast, daemon=True).start()

    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    env["NAV2_SHM_NAMESPACE"] = NAMESPACE

    print("test_shm.exe on the console, %d ballast threads, mask 0x%X, stall > %.0fs = hang"
          % (ballast_threads, mask, STALL_SECONDS), flush=True)

    try:
        for attempt in range(1, attempts + 1):
            print("---- attempt %d ----" % attempt, flush=True)
            began = time.monotonic()
            proc = subprocess.Popen([EXE], cwd=CWD, env=env)
            while True:
                rc = proc.poll()
                if rc is not None:
                    break
                if time.monotonic() - began > STALL_SECONDS:
                    print(">>> STALLED %.1fs - killing, the last line above is where it is"
                          % (time.monotonic() - began), flush=True)
                    proc.kill()
                    proc.wait()
                    return 1
                time.sleep(0.05)
            print("---- attempt %d rc=%d in %.1fs ----" % (attempt, rc, time.monotonic() - began),
                  flush=True)
    finally:
        stop.set()
    return 0


if __name__ == "__main__":
    sys.exit(main())
