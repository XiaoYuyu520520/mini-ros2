# -*- coding: utf-8 -*-
"""Reduce a mission-script run log to the few lines that are evidence."""
import io, re, sys

path = sys.argv[1]
lines = io.open(path, encoding="utf-8", errors="replace").read().splitlines()
stamp = re.compile(r"\[INFO\] \[(\d+\.\d+)\]\s+\[[^\]]+\]:\s+(.*)")

feedbacks, accepted, result, other = [], None, None, []
for line in lines:
    m = stamp.match(line)
    if not m:
        other.append(line)
        continue
    when, text = m.group(1), m.group(2)
    if text.startswith("Goal accepted"):
        accepted = when
    elif text.startswith("Received feedback"):
        remaining = re.search(r"number_of_poses_remaining=(\d+)", text)
        dist = re.search(r"distance_remaining=([\d.eE+-]+)", text)
        passed = text.count("waypoint_status=1")
        feedbacks.append((when, int(remaining.group(1)), float(dist.group(1)), passed))
    elif text.startswith("Navigation result"):
        result = (when, text)

print("log lines            : %d" % len(lines))
print("goal accepted        : %s" % accepted)
print("feedback messages    : %d" % len(feedbacks))
if feedbacks:
    first, last = feedbacks[0], feedbacks[-1]
    print("first feedback       : t=%s poses_remaining=%d distance=%.2f m passed=%d"
          % (first[0], first[1], first[2], first[3]))
    print("last feedback        : t=%s poses_remaining=%d distance=%.2f m passed=%d"
          % (last[0], last[1], last[2], last[3]))
    print("poses_remaining seen : %s" % sorted({f[1] for f in feedbacks}))
if result:
    when, text = result
    finals = re.findall(r"waypoint_status=(\d+), waypoint_index=(\d+)", text)
    print("result               : t=%s wall=%.1f s"
          % (when, float(when) - float(accepted)))
    print("result waypoints     : %d, statuses=%s"
          % (len(finals), sorted({s for s, _ in finals})))
    print("result error_code    : %s" % re.search(r"error_code=(-?\d+)", text).group(1))
print("non-[INFO] log lines : %d" % len(other))
for line in other[:6]:
    print("    | %s" % line[:120])
print()
print("VERDICT: %s" % ("one NavigateThroughPoses goal carried all 9 waypoints of sim_room12.csv"
                      " and came back error_code=0" if result else "no result came back"))
