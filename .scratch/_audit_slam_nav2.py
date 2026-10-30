# -*- coding: utf-8 -*-
"""Audit run: slam_toolbox on Windows as the map source for nav2.

Daemon is restarted without /map so SLAM owns the topic, the robot is reset,
slam_bridge_node maps a live lap while the nav2 stack (--no-amcl) plans on that
map and drives.  Everything is timestamped so the evidence can be dated.
"""
import io
import json
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
BUILD = os.path.join(ROOT, "nav2", "ros2_shim", "build")
SC = os.path.join(ROOT, ".scratch")
WRAP = os.path.join(SC, "run_shim_exe.bat")
REPORT = os.path.join(SC, "audit_slam_nav2.json")
PGM = os.path.join(SC, "audit_slam_map")

LOG = []


def stamp():
    return time.strftime("%H:%M:%S")


def say(text):
    line = "[%s] %s" % (stamp(), text)
    print(line, flush=True)
    LOG.append(line)


def run(args, t=180):
    d = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=t)
    return ((d.stdout or "") + (d.stderr or "")).strip()


def ros2(*a, t=180):
    return run([os.path.join(BAT, "ros2.bat"), *a], t=t)


def mapbin(exe, *a, t=240):
    return run([WRAP, os.path.join(BUILD, exe), *a], t=t)


say("=== 0. clean slate ===")
say(run([os.path.join(BAT, "nav2_stack.bat"), "stop"], 240))
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"], 240))
time.sleep(2)

say("=== 1. daemon WITHOUT /map: slam_toolbox is the map source ===")
say(run([os.path.join(BAT, "ros2_bridge.bat"), "start", "--mirror", "/clock",
         "--mirror", "/odom", "--mirror", "/scan", "--mirror", "/tf",
         "--mirror", "/tf_static"], t=240))
deadline = time.time() + 150
listing = ""
while time.time() < deadline:
    listing = ros2("topic", "list")
    if "/scan" in listing and "/map" not in listing:
        break
    time.sleep(3)
say("mirrored topics: %s" % " ".join(sorted(l for l in listing.split() if l.startswith("/"))))

say("=== 2. robot back to the origin ===")
say(ros2("service", "call", "/sim/reset", "std_srvs/srv/Trigger"))

say("=== 3. slam_toolbox, /map, base_link ===")
slam_log = io.open(os.path.join(SC, "audit_slam_node.log"), "w", encoding="utf-8")
slam = subprocess.Popen(["cmd", "/c", os.path.join(BAT, "slam_demo.bat"),
                         "-p", "base_frame:=base_link",
                         "-p", "max_laser_range:=12.0",
                         "-p", "map_update_interval:=2.0",
                         "--duration", "420"],
                        cwd=ROOT, stdout=slam_log, stderr=subprocess.STDOUT)

deadline = time.time() + 90
map_info = ""
while time.time() < deadline:
    map_info = ros2("topic", "info", "/map", t=60)
    if "Publisher count: 1" in map_info and "local" in map_info:
        break
    time.sleep(4)
say("--- /map after SLAM start ---")
say(map_info)

say("=== 4. nav2 stack on the SLAM map (--no-amcl), goal auto ===")
if os.path.exists(REPORT):
    os.remove(REPORT)
stack_log = io.open(os.path.join(SC, "audit_slam_nav2_stack.log"), "w", encoding="utf-8")
stack = subprocess.Popen(["cmd", "/c", os.path.join(BAT, "nav2_stack.bat"),
                          "--no-amcl", "--goal", "auto", "--timeout", "90",
                          "--report", REPORT],
                         cwd=ROOT, stdout=stack_log, stderr=subprocess.STDOUT)

deadline = time.time() + 260
while time.time() < deadline:
    if os.path.exists(REPORT):
        break
    time.sleep(5)
time.sleep(3)
say("--- nav2 node list during/after the run ---")
say(ros2("node", "list", t=60))
say("--- /plan ---")
say(ros2("topic", "info", "/plan", t=60))
say("--- /map publisher count (SLAM) ---")
say(ros2("topic", "info", "/map", t=60))
say("--- /cmd_vel ---")
say(ros2("topic", "info", "/cmd_vel", t=60))

say("=== 5. the goal report ===")
if os.path.exists(REPORT):
    say(io.open(REPORT, encoding="utf-8", errors="replace").read())
else:
    say("no report written: the goal never finished inside the budget")

say("=== 6. dump the SLAM map ===")
say(mapbin("map_dump.exe", "--topic", "/map", "--settle", "6", "--ascii", "56",
           "--out", PGM + ".pgm"))

say("=== 7. slam node log tail ===")
say(io.open(os.path.join(SC, "audit_slam_node.log"), encoding="utf-8",
            errors="replace").read()[-2500:])

say("=== 8. cleanup ===")
say(run([os.path.join(BAT, "nav2_stack.bat"), "stop"], 240))
slam.kill()
slam_log.close()
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"], 240))
time.sleep(2)
say(run([os.path.join(BAT, "ros2_bridge.bat"), "start"], t=240))
deadline = time.time() + 120
while time.time() < deadline:
    if "/scan" in ros2("topic", "list", t=60):
        break
    time.sleep(3)
say("--- restored default daemon topics ---")
say(ros2("topic", "list", t=60))

io.open(os.path.join(SC, "audit_slam_nav2.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\audit_slam_nav2.out")