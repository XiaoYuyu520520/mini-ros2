# -*- coding: utf-8 -*-
"""Hold one channel mutex from outside, then let go: does test_shm.com resume?

Found by accident. The first version of the channel prober asked
`WaitForSingleObject(OpenMutexA("Local\\nav2shm_test_timeout_mutex"), 0)`, which
does not *inspect* a mutex - it *acquires* it. The probe held it, and
`test_shm.exe` stopped dead: 161 s of wall clock, 0 s of CPU, blocked in
`publish()` -> `WaitForSingleObject(impl.mutex, INFINITE)`
(`shm_transport.cpp:341`), which never has a timeout.

So this reproduces it on purpose, and proves the cause by releasing the mutex
again:

  * the channel names are fixed constants in test_shm.cpp (`test_timeout`,
    `test_drop`, `test_broadcast`, `test_throughput`, `test_xproc`) and
    `NAV2_SHM_NAMESPACE` is *not* applied to them - only to the names the shim
    builds from topic names. `Local\\` is per logon session, not per test run.
    Any second process that touches the same name - a second run, a leftover
    child, a tool - can therefore take the mutex.
  * if the process is stuck, it resumes the moment the holder releases.

    python .scratch/_mutex_hold_ab.py [hold_seconds]
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
EXE = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\shm_transport\test_shm.exe"
CWD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build\shm_transport"
CASE = "test_timeout"
MUTEX = ("Local\\nav2shm_%s_mutex" % CASE).encode("ascii")
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


def cpu_seconds(pid: int) -> float:
    class FILETIME(ctypes.Structure):
        _fields_ = [("low", wt.DWORD), ("high", wt.DWORD)]

    k32.OpenProcess.restype = wt.HANDLE
    handle = k32.OpenProcess(0x0400, False, pid)  # PROCESS_QUERY_INFORMATION
    if not handle:
        return -1.0
    creation, exit_t, kernel, user = FILETIME(), FILETIME(), FILETIME(), FILETIME()
    ok = k32.GetProcessTimes(handle, ctypes.byref(creation), ctypes.byref(exit_t),
                             ctypes.byref(kernel), ctypes.byref(user))
    k32.CloseHandle(handle)
    if not ok:
        return -1.0
    def seconds(ft):
        return (ft.high << 32 | ft.low) / 1e7
    return seconds(kernel) + seconds(user)


def main() -> int:
    hold = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    env = dict(os.environ)
    env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")

    print("control: run it with nobody holding the mutex", flush=True)
    t0 = time.monotonic()
    subprocess.run([EXE], cwd=CWD, env=env, capture_output=True, text=True, timeout=60)
    print("  finished in %.2fs" % (time.monotonic() - t0), flush=True)

    print("\nnow run it while a python thread holds %s for %.0fs"
          % (MUTEX.decode(), hold), flush=True)
    proc = subprocess.Popen([EXE], cwd=CWD, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    began = time.monotonic()

    got = threading.Event()
    held_info = {}

    def holder() -> None:
        # The channel has to exist before the mutex can be opened, so wait for it.
        while time.monotonic() - began < 20.0:
            if k32.OpenFileMappingA(4, False,
                                    ("Local\\nav2shm_%s_data" % CASE).encode("ascii")):
                break
            time.sleep(0.002)
        handle = k32.OpenMutexA(SYNCHRONIZE | 0x0001, False, MUTEX)
        if not handle:
            held_info["error"] = "OpenMutexA failed: %d" % ctypes.get_last_error()
            got.set()
            return
        result = k32.WaitForSingleObject(handle, 2000)
        held_info["acquired"] = result
        got.set()
        time.sleep(hold)
        if result in (WAIT_OBJECT_0, WAIT_ABANDONED):
            k32.ReleaseMutex(handle)
            held_info["released"] = True
        k32.CloseHandle(handle)

    thread = threading.Thread(target=holder, daemon=True)
    thread.start()
    got.wait(timeout=25.0)

    time.sleep(2.0)
    stuck_cpu = cpu_seconds(proc.pid)
    stuck_alive = proc.poll() is None
    print("  while held: alive=%s cpu=%.3fs (%.1fs wall)"
          % (stuck_alive, stuck_cpu, time.monotonic() - began), flush=True)

    thread.join(timeout=hold + 5.0)
    try:
        out, _ = proc.communicate(timeout=30)
        rc = proc.returncode
        resumed = True
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
        rc = -1
        resumed = False
    total = time.monotonic() - began
    if isinstance(out, bytes):
        out = out.decode("utf-8", "replace")
    tail = [l for l in out.splitlines() if l.strip()][-3:]
    print("  after release: resumed=%s rc=%d wall=%.2fs" % (resumed, rc, total), flush=True)
    for line in tail:
        print("    | " + line.strip(), flush=True)
    print("  holder: %s" % held_info, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
