#!/usr/bin/env python3
"""Scan slam_toolbox sources for external/ROS API surface."""
import os, re, sys
from collections import Counter

ROOT = r"E:\DesktopData\app\code\CPU\nav2\third_party\slam_toolbox"
INTEREST = (
    "rclcpp", "tf2", "message_filters", "pluginlib", "nav2", "nav_msgs",
    "sensor_msgs", "visualization_msgs", "std_srvs", "std_msgs", "geometry_msgs",
    "lifecycle_msgs", "bond", "interactive", "rviz", "Poco", "Eigen", "ceres",
    "boost", "tbb", "slam_toolbox", "karto", "Qt",
)
INC_RE = re.compile(r"#include\s*[<\"]([^>\"]+)[>\"]")

counts = Counter()
poco = []
ros_api = Counter()
ros_syms = [
    "create_publisher", "create_subscription", "create_service", "create_wall_timer",
    "declare_parameter", "get_parameter", "LifecycleNode", "rclcpp_lifecycle",
    "create_client", "message_filters::", "tf2_ros::", "pluginlib::",
    "bond::", "interactive_markers", "rviz_common", "RCLCPP_",
]

for r, d, fs in os.walk(ROOT):
    d[:] = [x for x in d if x not in (".git",)]
    for f in fs:
        if not f.endswith((".h", ".hpp", ".cpp", ".hxx", ".cc")):
            continue
        p = os.path.join(r, f)
        rel = os.path.relpath(p, ROOT)
        t = open(p, encoding="utf-8", errors="ignore").read()
        for m in INC_RE.finditer(t):
            i = m.group(1)
            if i.startswith(INTEREST):
                counts[i] += 1
                if i.startswith("Poco"):
                    poco.append((i, rel))
        for s in ros_syms:
            n = t.count(s)
            if n:
                ros_api[s] += n

print("=== external includes (count of occurrences) ===")
for k, v in sorted(counts.items()):
    print("%5d  %s" % (v, k))
print()
print("=== Poco users ===")
for i, rel in poco:
    print("  %-24s %s" % (i, rel))
print()
print("=== ROS API symbol usage ===")
for k, v in sorted(ros_api.items(), key=lambda kv: -kv[1]):
    print("%5d  %s" % (v, k))
