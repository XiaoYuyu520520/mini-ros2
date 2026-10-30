# -*- coding: utf-8 -*-
"""Live SLAM over the bridge: daemon without /map, slam_toolbox builds /slam_map
while the Ubuntu car drives a square, then map_dump.exe pulls the grid out.

Every command goes through a shell with bridge\\windows as the cwd: passing
`cmd /c` an argv list makes cmd see the whole thing as one quoted filename and
fail with "The filename, directory name, or volume label syntax is incorrect."
"""
import os
import subprocess
import time

ROOT = r"E:\DesktopData\app\code\CPU"
WIN = os.path.join(ROOT, "bridge", "windows")
SC = os.path.join(ROOT, ".scratch")

LOG = []


def say(text):
    print(text, flush=True)
    LOG.append(text)


def bat(command, timeout=180):
    """bridge_env.bat, then the command, in a fresh shell rooted at bridge\\windows."""
    done = subprocess.run("call bridge_env.bat && " + command, shell=True, cwd=WIN,
                          capture_output=True, text=True, errors="replace", timeout=timeout)
    return (done.stdout or "") + (done.stderr or "")


say("=== 1. restart the daemon without /map, so SLAM owns the map ===")
say(bat("ros2_bridge.bat stop").strip())
time.sleep(2)
say(bat("ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan "
        "--mirror /tf --mirror /tf_static").strip())

deadline = time.time() + 90
listing = ""
while time.time() < deadline:
    listing = bat("ros2.bat topic list -t", timeout=60)
    if "/scan" in listing and "/tf" in listing:
        break
    time.sleep(3)
say("--- topic list -t ---")
say(listing.strip())

say("=== 2. put the car back at the origin ===")
say(bat("ros2.bat service call /sim/reset std_srvs/srv/Trigger").strip())

say("=== 3. slam_toolbox on the Windows side ===")
slam_log = open(os.path.join(SC, "slam_live.log"), "w", encoding="utf-8")
proc = subprocess.Popen(
    "slam_demo.bat -p map_name:=/slam_map --duration 90",
    shell=True, cwd=WIN, stdout=slam_log, stderr=subprocess.STDOUT)
say("slam_demo.bat pid %d" % proc.pid)
time.sleep(16)

say("=== 4. drive the car around a square ===")
for leg in range(4):
    drove = bat("ros2.bat action send_goal /sim/drive ros2_bridge_sim/action/Drive "
                "distance=2.0 --timeout 45", timeout=120)
    tail = [l for l in drove.splitlines() if "SUCCEEDED" in l or "result" in l.lower()]
    say("  leg %d drive: %s" % (leg + 1, tail[-1].strip() if tail else drove.strip()[-140:]))
    turned = bat("ros2.bat action send_goal /sim/spin nav2_msgs/action/Spin "
                 "target_yaw=1.5708 --timeout 30", timeout=90)
    tail = [l for l in turned.splitlines() if "SUCCEEDED" in l or "result" in l.lower()]
    say("  leg %d turn : %s" % (leg + 1, tail[-1].strip() if tail else turned.strip()[-140:]))

say(bat("ros2.bat service call /sim/status std_srvs/srv/Trigger").strip())
time.sleep(5)

say("=== 5. pull /slam_map out of shared memory ===")
say(bat("map_dump.exe --topic /slam_map --settle 6 --out .scratch\\slam_live_map.pgm --ascii 64",
        timeout=180).strip())

say("=== 6. what the node itself logged ===")
try:
    proc.wait(timeout=90)
except subprocess.TimeoutExpired:
    proc.kill()
slam_log.close()
tail = open(os.path.join(SC, "slam_live.log"), encoding="utf-8", errors="replace").read()
say(tail[-3000:])

say("=== 7. stop the daemon ===")
say(bat("ros2_bridge.bat stop").strip())

open(os.path.join(SC, "slam_live.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\slam_live.out")