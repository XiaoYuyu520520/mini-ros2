# -*- coding: utf-8 -*-
"""Run the three lap scripts the documented way and check that they really lap.

    python .scratch/run_lap_batch.py --laps 2

For every script: ``stop`` -> ``/sim/reset`` -> ``nav --map live`` -> ``run
<script> <csv> --laps N``. The point of ``--laps 2`` is that two laps is the
first count the old resend-the-same-goal spelling could not do: the second lap
came back as a success in 0.08 s without the robot moving (evidence 78). So a
run that takes minutes, reports ``current_waypoint`` wrapping back to 0, and
then exits non-timeout is the thing being verified.

Each run lands in ``.scratch/lap_batch_<tag>.log``; the verdicts are appended to
``.scratch/lap_batch_summary.txt`` as they finish.
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

#: script, route, and what its own header says it should do
SPECS = [
    ("重复载入完成圈数.py", "sim_room12.csv"),
    ("会跳过几个点.py", "sim_room12.csv"),
    ("ai_第一个点后回终点.py", "sim_room12.csv"),
]

STAMP = re.compile(r"\[INFO\] \[(\d+\.\d+)\]\s+\[[^\]]+\]:\s+(.*)")


def step(argv, fh, timeout):
    fh.write("\n$ %s\n" % " ".join(str(a) for a in argv))
    fh.flush()
    started = time.time()
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
    wall = time.time() - started
    fh.write("\n-- exit %s after %.1f s\n" % (code, wall))
    fh.flush()
    return code, wall


def verdict(path):
    """The lines of a run log that are evidence, in the order they happened."""
    lines = io.open(path, encoding="utf-8", errors="replace").read().splitlines()
    accepted = None
    waypoints = []
    laps = 0
    result = None
    last = None
    errors = []
    for line in lines:
        m = STAMP.match(line)
        if not m:
            if line.startswith("[ERROR]") or line.startswith("[WARN]"):
                errors.append(line[:160])
            continue
        when, text = m.group(1), m.group(2)
        if text.startswith("Goal accepted"):
            accepted = when
        elif text.startswith("Waypoint "):
            index = int(text.split()[1].split("/")[0])
            slot = text.split("/")[1]
            if last is not None and index <= last:
                laps += 1
            last = index
            waypoints.append((when, index))
        elif "Navigation result" in text:
            result = (when, text)
        elif text.startswith("Goal rejected"):
            result = (when, text)
    out = ["  goal accepted %s, %d waypoint report(s)" % (accepted or "NO", len(waypoints))]
    if waypoints:
        out.append("  first waypoint %s, last %s" % (waypoints[0], waypoints[-1]))
    out.append("  wraps back to waypoint 1: %d" % laps)
    if result:
        wall = float(result[0]) - float(accepted) if accepted else -1.0
        out.append("  result %s  wall=%.1f s  %s" % (result[0], wall, result[1][:90]))
    else:
        out.append("  result: NONE")
    out.append("  [ERROR]/[WARN] lines %d" % len(errors))
    for line in errors[:3]:
        out.append("  | %s" % line)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--laps", type=int, default=2, help="laps per script")
    parser.add_argument("--timeout", type=int, default=420, help="per script run")
    parser.add_argument("--nav-timeout", type=int, default=420)
    parser.add_argument("--only", default=None, help="substring of the script name")
    args = parser.parse_args()

    summary = os.path.join(SCRATCH, "lap_batch_summary.txt")
    with io.open(summary, "a", encoding="utf-8", newline="\n") as sh:
        sh.write("\n===== lap batch %s  laps=%d =====\n"
                 % (time.strftime("%Y-%m-%d %H:%M:%S"), args.laps))
        for index, (name, csv) in enumerate(SPECS):
            if args.only and args.only not in name:
                continue
            tag = "l%02d" % index
            log = os.path.join(SCRATCH, "lap_batch_%s.log" % tag)
            print("[lap] %s  %s  %s" % (tag, name, csv), flush=True)
            with io.open(log, "w", encoding="utf-8", newline="\n") as fh:
                fh.write("# %s : %s %s  --laps %d\n" % (tag, name, csv, args.laps))
                step([PY, CTL, "stop"], fh, 180)
                step(["cmd", "/c", BAT, "service", "call", "/sim/reset",
                      "std_srvs/srv/Trigger"], fh, 120)
                nav_code, _ = step([PY, CTL, "nav", "--map", "live"], fh, args.nav_timeout)
                if nav_code != 0:
                    fh.write("!! nav exited %s; running anyway\n" % nav_code)
                run_code, wall = step([PY, CTL, "run", "--sim-time", name, csv,
                                       "--laps", str(args.laps)], fh, args.timeout)
            sh.write("\n--- %s  %s %s  --laps %d  exit=%s wall=%.1f s\n"
                     % (tag, name, csv, args.laps, run_code, wall))
            for line in verdict(log):
                sh.write(line + "\n")
            sh.flush()
            print("[lap] %s exit=%s wall=%.1f s" % (tag, run_code, wall), flush=True)


if __name__ == "__main__":
    main()
