# -*- coding: utf-8 -*-
"""Run the `NavigateThroughPoses` scripts the documented way, on the shim.

    python .scratch/run_ntp_dual.py

`stop` -> `/sim/reset` -> `nav --map live` -> `run --sim-time <script> <csv>`,
one log per script in `.scratch/ntp_dual_<tag>.log` and the verdicts appended to
`.scratch/ntp_dual_summary.txt`.

The point is the `goal_msg.poses` branch: every one of these scripts used to
write `goal_msg.poses.goals = poses` unconditionally, which is the ported nav2
1.5.1 spelling (`nav_msgs/Goals`) and an `AttributeError` on Jazzy, where
`poses` is a plain `geometry_msgs/PoseStamped[]`. `.scratch/probe_ntp_poses.py`
prints which arm each platform takes; this runs the arm the shim takes, all the
way to a nav2 result, so "the branch still works on the side it was written
for" is measured and not assumed.
"""
from __future__ import annotations

import argparse
import io
import os
import re
import subprocess
import sys
import time

ROOT = r"E:\DesktopData\app\code\CPU"
PY = r"E:\venvs\py312\Scripts\python.exe"
CTL = os.path.join(ROOT, "code", "Desktop", "racecar", "tools", "racecar_ctl.py")
BAT = os.path.join(ROOT, "bridge", "windows", "ros2.bat")
SCRATCH = os.path.join(ROOT, ".scratch")

#: script, route. `detct.py` and the lap scripts are left out on purpose: the
#: first needs a second goal from `/goal_pose`, the others are `FollowWaypoints`
#: and went through `.scratch/run_lap_batch.py` instead.
SPECS = [
    ("end_3_12.py", "sim_room12.csv"),
    ("终点不停.py", "sim_room12.csv"),
]

STAMP = re.compile(r"\[INFO\] \[(\d+\.\d+)\]\s+\[[^\]]+\]:\s+(.*)")


def step(argv, fh, timeout):
    fh.write("\n$ %s\n" % " ".join(str(a) for a in argv))
    fh.flush()
    began = time.time()
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    proc = subprocess.Popen([str(a) for a in argv], cwd=ROOT, stdout=fh,
                            stderr=subprocess.STDOUT, env=env)
    try:
        code = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        fh.write("\n!! killed after %s s\n" % timeout)
        code = -9
    wall = time.time() - began
    fh.write("\n-- exit %s after %.1f s\n" % (code, wall))
    fh.flush()
    return code, wall


def verdict(path):
    lines = io.open(path, encoding="utf-8", errors="replace").read().splitlines()
    accepted = None
    result = None
    errors = []
    for line in lines:
        match = STAMP.match(line)
        if not match:
            if line.startswith(("[ERROR]", "[WARN]", "Traceback", "AttributeError")):
                errors.append(line[:160])
            continue
        when, text = match.group(1), match.group(2)
        if text.startswith("Goal accepted"):
            accepted = when
        elif "Navigation result" in text:
            result = (when, text)
        elif text.startswith("Goal rejected"):
            result = (when, text)
    out = ["  goal accepted %s" % (accepted or "NO")]
    if result:
        wall = float(result[0]) - float(accepted) if accepted else -1.0
        out.append("  result %s  wall=%.1f s  %s" % (result[0], wall, result[1][:110]))
    else:
        out.append("  result: NONE")
    out.append("  [ERROR]/[WARN]/traceback lines %d" % len(errors))
    for line in errors[:3]:
        out.append("  | %s" % line)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=int, default=420)
    parser.add_argument("--nav-timeout", type=int, default=420)
    parser.add_argument("--only", default=None)
    args = parser.parse_args()

    with io.open(os.path.join(SCRATCH, "ntp_dual_summary.txt"), "a",
                 encoding="utf-8", newline="\n") as summary:
        summary.write("\n===== ntp dual %s =====\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
        for index, (name, route) in enumerate(SPECS):
            if args.only and args.only not in name:
                continue
            tag = "n%02d" % index
            log = os.path.join(SCRATCH, "ntp_dual_%s.log" % tag)
            print("[ntp] %s  %s  %s" % (tag, name, route), flush=True)
            with io.open(log, "w", encoding="utf-8", newline="\n") as fh:
                fh.write("# %s : %s %s\n" % (tag, name, route))
                step([PY, CTL, "stop"], fh, 180)
                step(["cmd", "/c", BAT, "service", "call", "/sim/reset",
                      "std_srvs/srv/Trigger"], fh, 120)
                nav_code, _ = step([PY, CTL, "nav", "--map", "live"], fh, args.nav_timeout)
                if nav_code != 0:
                    fh.write("!! nav exited %s; running anyway\n" % nav_code)
                run_code, wall = step([PY, CTL, "run", "--sim-time", name, route],
                                      fh, args.timeout)
            summary.write("\n--- %s  %s %s  exit=%s wall=%.1f s\n" % (tag, name, route, run_code, wall))
            for line in verdict(log):
                summary.write(line + "\n")
            summary.flush()
            print("[ntp] %s exit=%s wall=%.1f s" % (tag, run_code, wall), flush=True)


if __name__ == "__main__":
    main()
