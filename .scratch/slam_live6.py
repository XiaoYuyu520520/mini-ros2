# -*- coding: utf-8 -*-
"""Live SLAM over the gRPC bridge, third pass.

Two bugs killed the earlier attempts at step 5:

  1. the map tools were launched as `cmd /c "call bridge_env.bat && ..."`.
     cmd expands %VAR% when it parses the *whole* line, so
     %ROS2_BRIDGE_CPU_ROOT% was still empty by the time the line ran and the
     path collapsed to "\nav2\..." -> "The system cannot find the path
     specified."  Fix: absolute paths from Python, argv list, no shell.
  2. quoted exe paths inside that same `cmd /c "..."` string get their last
     quote eaten by cmd's /c quote stripping.

This pass drives a closed lap, then pulls the same /slam_map out three ways:
map_dump.exe (PGM + ascii preview) and map_saver_cli.exe twice, --fmt pgm and
--fmt png, so the PNG coder added in step 4 is proven on a real SLAM map.
"""
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

LOG = []


def say(text):
    print(text, flush=True)
    LOG.append(text)


def run(args, t=120):
    d = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=t)
    return ((d.stdout or "") + (d.stderr or "")).strip()


def ros2(*a, t=150):
    return run([os.path.join(BAT, "ros2.bat"), *a], t=t)


def mapbin(exe, *a, t=180):
    return run([WRAP, os.path.join(BUILD, exe), *a], t=t)


PGM = os.path.join(SC, "slam_live6.pgm")
PNG = os.path.join(SC, "slam_live6_png")
PGM2 = os.path.join(SC, "slam_live6_pgm")

say("=== 1. daemon WITHOUT /map: slam_toolbox is the map source ===")
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"]))
time.sleep(2)
say(run([os.path.join(BAT, "ros2_bridge.bat"), "start", "--mirror", "/clock",
         "--mirror", "/odom", "--mirror", "/scan", "--mirror", "/tf",
         "--mirror", "/tf_static"], t=240))
deadline = time.time() + 150
listing = ""
while time.time() < deadline:
    listing = ros2("topic", "list")
    if "/scan" in listing:
        break
    time.sleep(3)
say("--- topic list -t ---")
say(ros2("topic", "list", "-t"))

say("=== 2. car back to the origin ===")
say(ros2("service", "call", "/sim/reset", "std_srvs/srv/Trigger"))

say("=== 3. slam_toolbox ===")
slam_log = open(os.path.join(SC, "slam_live6.log"), "w", encoding="utf-8")
slam = subprocess.Popen(["cmd", "/c", os.path.join(BAT, "slam_demo.bat"),
                         "-p", "map_name:=/slam_map",
                         "-p", "base_frame:=base_link",
                         "-p", "max_laser_range:=12.0",
                         "-p", "map_update_interval:=2.0",
                         "--duration", "170"],
                        cwd=ROOT, stdout=slam_log, stderr=subprocess.STDOUT)
time.sleep(18)

say("=== 4. one closed lap: 4 x (2 m straight + 90 deg) ===")
for leg in range(4):
    ros2("action", "send_goal", "/sim/drive", "ros2_bridge_sim/action/Drive",
         "distance=2.0", "--timeout", "45", t=180)
    ros2("action", "send_goal", "/sim/spin", "nav2_msgs/action/Spin",
         "target_yaw=1.5708", "--timeout", "30", t=120)
    say("  leg %d done" % (leg + 1))
say("--- /sim/status ---")
say(ros2("service", "call", "/sim/status", "std_srvs/srv/Trigger"))
time.sleep(8)

say("=== 5. the same map, three ways ===")
say("--- map_dump.exe -> PGM + ascii ---")
say(mapbin("map_dump.exe", "--topic", "/slam_map", "--settle", "6",
           "--ascii", "64", "--out", PGM))
say("--- map_saver_cli.exe --fmt pgm ---")
say(mapbin("map_saver_cli.exe", "-t", "/slam_map", "-f", PGM2, "--fmt", "pgm"))
say("--- map_saver_cli.exe --fmt png ---")
say(mapbin("map_saver_cli.exe", "-t", "/slam_map", "-f", PNG, "--fmt", "png"))

slam.kill()
slam_log.close()
say("=== 6. slam node log ===")
say(open(os.path.join(SC, "slam_live6.log"), encoding="utf-8", errors="replace").read()[-1600:])

say("=== 7. stop the daemon ===")
say(run([os.path.join(BAT, "ros2_bridge.bat"), "stop"]))

open(os.path.join(SC, "slam_live6.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\slam_live6.out")