# -*- coding: utf-8 -*-
"""Run ctest in the shim build dir, pinned to the six P-cores.

    python .scratch/run_ctest.py [jobs] [ctest args...]
"""
import ctypes
import os
import subprocess
import sys

CORES = [0, 1, 10, 11, 12, 13]
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.GetCurrentProcess.restype = ctypes.c_void_p
k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
mask = 0
for cpu in CORES:
    mask |= 1 << cpu
k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), mask)

UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
BUILD = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\build"
env = dict(os.environ)
env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")

jobs = sys.argv[1] if len(sys.argv) > 1 else "6"
extra = sys.argv[2:]
argv = [os.path.join(UCRT, "ctest.exe"), "--output-on-failure", "-j", jobs] + extra
log = r"E:\DesktopData\app\code\CPU\.scratch\ctest_png.log"
if "-j" + jobs == "-j1":
    log = r"E:\DesktopData\app\code\CPU\.scratch\ctest_png_serial.log"
done = subprocess.run(argv, cwd=BUILD, env=env, capture_output=True, text=True, timeout=900)
text = done.stdout + done.stderr
open(log, "w", encoding="utf-8").write(text)
print("exit %d, cores %s (mask 0x%X, pinned), log %s"
      % (done.returncode, ",".join(map(str, CORES)), mask, os.path.basename(log)))
for line in text.splitlines():
    if ("Test #" in line or "tests passed" in line or "tests failed" in line
            or "Total Test time" in line or "FAIL:" in line):
        print(line)