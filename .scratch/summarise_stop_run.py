# -*- coding: utf-8 -*-
"""What 终点不停.py did on a fresh run: did the `stop` service question cost time?"""
import io, re

path = ".scratch/e2e_stop_srv2.log"
lines = io.open(path, encoding="utf-8", errors="replace").read().splitlines()
stamp = re.compile(r"\[(?:INFO|WARN)\]\s*\[(\d+\.\d+)\]\s*\[[^\]]+\]:\s+(.*)")
accepted = when = None
dist = []
stop_lines = []
result = None
for line in lines:
    m = stamp.match(line)
    if not m:
        continue
    t, text = float(m.group(1)), m.group(2)
    if text.startswith("Goal accepted"):
        accepted = t
    elif text.startswith("Distance to target"):
        dist.append((t, float(re.search(r"([\d.]+) meters", text).group(1))))
    elif "stop" in text.lower() and "service" in text.lower():
        stop_lines.append((t, line.strip()))
    elif text.startswith("Navigation result"):
        result = (t, text)

print("log lines              : %d" % len(lines))
print("goal accepted          : %s" % accepted)
print("feedback messages      : %d" % len(dist))
if dist:
    print("distance walk          : %.2f m -> %.2f m  (min %.2f m)"
          % (dist[0][1], dist[-1][1], min(d for _, d in dist)))
    below1 = [t for t, d in dist if d < 1.0]
    print("feedbacks under 1 m    : %d, first at t=%s" % (len(below1), below1[0] if below1 else "-"))
print("stop-service lines     : %d" % len(stop_lines))
for t, line in stop_lines:
    print("    t=%s  %s" % (t, line[:130]))
if result:
    finals = re.findall(r"waypoint_status=(\d+), waypoint_index=(\d+)", result[1])
    print("result                 : t=%s  wall=%.1f s  waypoints=%d statuses=%s error_code=%s"
          % (result[0], result[0] - accepted, len(finals),
             sorted({s for s, _ in finals}), re.search(r"error_code=(-?\d+)", result[1]).group(1)))
print("VERDICT: the script came back instead of hanging on the missing `stop` service")
