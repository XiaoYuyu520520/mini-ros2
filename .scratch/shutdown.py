# -*- coding: utf-8 -*-
import io, os, subprocess, sys, time
sys.stdout.reconfigure(encoding="utf-8")
ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")

def run(args, t=120):
    d = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=t)
    return ((d.stdout or "") + (d.stderr or "")).strip()

print("--- nav2_stack.bat stop ---")
print(run([os.path.join(BAT, "nav2_stack.bat"), "stop"], t=180)[:800])
print("--- ros2_bridge.bat stop ---")
print(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"])[:400])
time.sleep(2)

import psutil
names = ("ros2_bridge_daemon.exe", "map_server.exe", "map_saver.exe", "lifecycle_manager.exe",
         "map_dump.exe", "planner_server.exe", "controller_server.exe", "bt_navigator.exe",
         "amcl.exe", "amcl_bridge_node.exe", "behavior_server.exe", "smoother_server.exe",
         "velocity_smoother.exe", "collision_monitor.exe", "waypoint_follower.exe",
         "slam_toolbox_node.exe", "sync_slam_toolbox_node.exe")
left = [p.info for p in psutil.process_iter(["pid", "name"])
        if (p.info["name"] or "").lower() in names]
print("--- shim processes still alive:", len(left))
for p in left:
    print("   ", p["pid"], p["name"])