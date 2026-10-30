# -*- coding: utf-8 -*-
"""Audit run 2: SLAM maps a lap, then nav2 (--no-amcl) plans on that map.

Run 1 showed the ordering matters: with a fresh, empty SLAM map `nav2_goal
--goal auto` has nothing to pick a goal from and gives up in 1.7 s.  This run
drives a closed lap first (so the map has walls in it), then hands an explicit
goal to the stack.
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
REPORT = os.path.join(SC, "audit_slam_nav2d.json")
PGM = os.path.join(SC, "audit_slam_map4")

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
run([r"taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
say(run([os.path.join(BAT, "nav2_stack.bat"), "stop"], 240))
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"], 240))
time.sleep(2)

say("=== 1. daemon WITHOUT /map: slam_toolbox owns the map topic ===")
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
say("mirrored: %s" % " ".join(sorted(l for l in listing.split() if l.startswith("/"))))

say("=== 2. robot to the origin ===")
say(ros2("service", "call", "/sim/reset", "std_srvs/srv/Trigger"))

say("=== 3. slam_toolbox ===")
slam_log = io.open(os.path.join(SC, "audit_slam_node4.log"), "w", encoding="utf-8")
slam = subprocess.Popen(["cmd", "/c", os.path.join(BAT, "slam_demo.bat"),
                         "-p", "base_frame:=base_link",
                         "-p", "max_laser_range:=12.0",
                         "-p", "map_update_interval:=2.0",
                         "--duration", "600"],
                        cwd=ROOT, stdout=slam_log, stderr=subprocess.STDOUT)
deadline = time.time() + 90
while time.time() < deadline:
    info = ros2("topic", "info", "/map", t=60)
    if "Publisher count: 1" in info and "local" in info:
        break
    time.sleep(4)
say("SLAM is publishing /map")

say("=== 4. map a closed lap: 4 x (2 m + 90 deg) ===")
for leg in range(4):
    ros2("action", "send_goal", "/sim/drive", "ros2_bridge_sim/action/Drive",
         "distance=2.0", "--timeout", "45", t=180)
    ros2("action", "send_goal", "/sim/spin", "nav2_msgs/action/Spin",
         "target_yaw=1.5708", "--timeout", "30", t=120)
    say("  leg %d done" % (leg + 1))
say(ros2("service", "call", "/sim/status", "std_srvs/srv/Trigger"))
time.sleep(6)

say("=== 5. the SLAM map after the lap ===")
say(mapbin("map_dump.exe", "--topic", "/map", "--settle", "6", "--ascii", "56",
           "--out", PGM + ".pgm"))

say("=== 6. nav2 on the SLAM map (--no-amcl), explicit goal ===")
if os.path.exists(REPORT):
    os.remove(REPORT)
stack_log = io.open(os.path.join(SC, "audit_slam_nav2d_stack.log"), "w", encoding="utf-8")
goal_line = ('cmd /c %s --no-amcl --goal "1.0,-1.0" --timeout 120 --report %s'
             % (os.path.join(BAT, "nav2_stack.bat"), REPORT))
say("command line: %s" % goal_line)
stack = subprocess.Popen(goal_line, cwd=ROOT, stdout=stack_log,
                         stderr=subprocess.STDOUT)

deadline = time.time() + 300
while time.time() < deadline:
    if os.path.exists(REPORT):
        break
    time.sleep(5)
time.sleep(4)

say("--- node list ---")
say(ros2("node", "list", t=60))
say("=== 7. the goal report ===")
if os.path.exists(REPORT):
    say(io.open(REPORT, encoding="utf-8", errors="replace").read())
else:
    say("no report: the goal never finished inside the budget")

say("=== 8. the map again, after nav2 drove on it ===")
say(mapbin("map_dump.exe", "--topic", "/map", "--settle", "6", "--ascii", "56",
           "--out", PGM + "_after.pgm"))

say("=== 9. slam node log tail ===")
say(io.open(os.path.join(SC, "audit_slam_node4.log"), encoding="utf-8",
            errors="replace").read()[-2500:])

say("=== 10. cleanup ===")
say(run([os.path.join(BAT, "nav2_stack.bat"), "stop"], 240))
slam.kill()
run([r"taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
slam_log.close()
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"], 240))
time.sleep(2)
say(run([os.path.join(BAT, "ros2_bridge.bat"), "start"], t=240))
deadline = time.time() + 120
while time.time() < deadline:
    if "/scan" in ros2("topic", "list", t=60):
        break
    time.sleep(3)
say("--- restored default daemon ---")
say(ros2("topic", "list", t=60))

io.open(os.path.join(SC, "audit_slam_nav2d.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\audit_slam_nav2d.out")