# -*- coding: utf-8 -*-
"""Hold a channel mutex from outside while running a test exe; dump ALL output.

    python .scratch/_hold_probe.py EXE CASE HOLD_SECONDS [CHILD_NAMESPACE]

CASE is the raw name that goes into `Local\nav2shm_<CASE>_mutex`; CHILD_NAMESPACE
is exported to the child as NAV2_SHM_NAMESPACE, which is what decides the name
the *test* actually uses. Holding `test_timeout` while the child runs with a
namespace is therefore the isolation check: if the child is unaffected, the two
names really are different channels.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import threading
import time

UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
SYNCHRONIZE = 0x00100000
WAIT_OBJECT_0 = 0
WAIT_ABANDONED = 0x80

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenMutexA.restype = wt.HANDLE
k32.OpenMutexA.argtypes = [wt.DWORD, wt.BOOL, ctypes.c_char_p]
k32.OpenFileMappingA.restype = wt.HANDLE
k32.OpenFileMappingA.argtypes = [wt.DWORD, wt.BOOL, ctypes.c_char_p]
k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
k32.ReleaseMutex.argtypes = [wt.HANDLE]
k32.CloseHandle.argtypes = [wt.HANDLE]


def main() -> int:
    exe, case, hold = sys.argv[1], sys.argv[2], float(sys.argv[3])
    child_ns = sys.argv[4] if len(sys.argv) > 4 else ""
    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
    if child_ns:
        env["NAV2_SHM_NAMESPACE"] = child_ns
    workdir = os.path.dirname(os.path.abspath(exe))
    proc = subprocess.Popen([exe], cwd=workdir, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    began = time.monotonic()
    ready = threading.Event()
    info = {"case": case, "child_ns": child_ns or "(none)"}

    def holder() -> None:
        while time.monotonic() - began < 20.0:
            probe = k32.OpenFileMappingA(4, False, ("Local\\nav2shm_%s_data" % case).encode("ascii"))
            if probe:
                k32.CloseHandle(probe)
                break
            time.sleep(0.002)
        handle = k32.OpenMutexA(SYNCHRONIZE | 0x0001, False,
                                ("Local\\nav2shm_%s_mutex" % case).encode("ascii"))
        if not handle:
            info["error"] = ctypes.get_last_error()
            ready.set()
            return
        result = k32.WaitForSingleObject(handle, 2000)
        info["acquired"] = result
        ready.set()
        time.sleep(hold)
        if result in (WAIT_OBJECT_0, WAIT_ABANDONED):
            k32.ReleaseMutex(handle)
            info["released_at"] = round(time.monotonic() - began, 3)
        k32.CloseHandle(handle)

    thread = threading.Thread(target=holder, daemon=True)
    thread.start()
    ready.wait(timeout=25.0)
    print("holder: %s" % info, flush=True)

    try:
        out, _ = proc.communicate(timeout=hold + 60.0)
        rc, timed_out = proc.returncode, False
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
        rc, timed_out = -1, True
    wall = time.monotonic() - began
    if isinstance(out, bytes):
        out = out.decode("utf-8", "replace")
    print("exe finished: rc=%d wall=%.2fs (mutex released at t=%.1fs, hold=%.1fs) timed_out=%s"
          % (rc, wall, info.get("released_at", -1.0), hold, timed_out), flush=True)
    print("---- full exe output ----", flush=True)
    print(out, flush=True)
    print("---- end ----", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())