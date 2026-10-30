#!/usr/bin/env python3
"""Smoke-test the bat -> ps1 -> window handover.

`ros2_shell.bat` opens a new PowerShell window and expects `ros2.ps1` to make it
usable. The window itself cannot be inspected from here, so this writes the same
launch line into a throwaway bat, runs it with `-Command` instead of `-NoExit`
(so the window closes by itself), and checks that `ros2 topic list -t` really
produced the bridged topics inside it.

    python .scratch/ros2shell_launch.py

Exit code 0 means the spawned window was prepared and could talk to the bridge.
"""
import io
import os
import subprocess
import sys
import time

SCRATCH = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRATCH)
PS1 = os.path.join(ROOT, "bridge", "windows", "ros2.ps1")
PROBE_BAT = os.path.join(SCRATCH, "ros2shell_probe.bat")
OUT = os.path.join(SCRATCH, "ros2shell_probe_out.txt")
EXPECTED = ["/clock", "/cmd_vel", "/map", "/odom", "/scan", "/tf", "/tf_static"]


def main():
    for path in (PROBE_BAT, OUT):
        if os.path.exists(path):
            os.remove(path)

    probe = ("@echo off\n"
             "start \"ROS2 probe\" /D \"%s\" \"pwsh.exe\" -Command \". '%s'; ros2 topic list -t > '%s'\"\n"
             % (SCRATCH, PS1, OUT))
    io.open(PROBE_BAT, "wb").write(probe.replace("\n", "\r\n").encode("utf-8"))

    subprocess.run(["cmd", "/c", PROBE_BAT], check=False)
    for _ in range(60):
        time.sleep(0.5)
        if os.path.exists(OUT) and os.path.getsize(OUT):
            break

    if not os.path.exists(OUT):
        print("FAIL: the spawned window never wrote its output (no window started?)")
        return 1

    text = io.open(OUT, encoding="utf-8", errors="replace").read()
    print(text.rstrip())
    missing = [name for name in EXPECTED if name not in text]
    if missing:
        print("FAIL: the window graph is missing %s" % ", ".join(missing))
        return 1
    print("\nOK: the window ros2_shell.bat opens is prepared and sees all %d topics"
          % len(EXPECTED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
