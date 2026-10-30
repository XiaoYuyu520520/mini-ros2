# -*- coding: utf-8 -*-
import os
import subprocess
import sys

ROOT = r"E:\DesktopData\app\code\CPU"
PY = r"E:\venvs\py312\Scripts\python.exe"
RUNNER = os.path.join(ROOT, "nav2", "ros2_shim", "python", "shim_run.py")
PROBE = os.path.join(ROOT, ".scratch", "probe_plan.py")

START = ("0.79", "0.50", "-0.95")
GOALS = [
    ("1.00", "0.50", "0.0"),
    ("1.00", "1.50", "0.0"),
    ("2.00", "1.00", "0.0"),
    ("1.00", "1.50", "1.57"),
    ("1.50", "1.00", "0.0"),
    ("0.50", "0.50", "0.0"),
    ("1.00", "0.00", "0.0"),
    ("2.55", "0.00", "0.0"),
    ("1.00", "0.90", "0.0"),
]
for g in GOALS:
    args = [PY, "-X", "utf8", RUNNER, PROBE] + list(START) + list(g) + ["1"]
    out = subprocess.run(args, capture_output=True, text=True, timeout=180).stdout
    line = [l for l in out.splitlines() if l.startswith("PROBE 0")]
    print("goal=%-18s %s" % (",".join(g), line[0] if line else "NO OUTPUT"), flush=True)
