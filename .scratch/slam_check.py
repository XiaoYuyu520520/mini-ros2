"""Fresh end-to-end SLAM check: /scan + /tf -> slam_bridge_node -> /map."""

import os
import subprocess
import time

ROOT = r"E:\DesktopData\app\code\CPU"
SCRATCH = os.path.join(ROOT, ".scratch")
MAP_DUMP = r"nav2\ros2_shim\build\map_dump.exe"
GRAPH_DIR = os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "nav2_shim_graph")


def log(*a):
    print("SLAMCHK", *a, flush=True)


def sh(cmd, timeout=300):
    p = subprocess.run(["cmd", "/c", cmd], cwd=ROOT, capture_output=True,
                       text=True, errors="replace", timeout=timeout)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def run_env(cmd, timeout=300):
    return sh("call bridge\\windows\\bridge_env.bat && " + cmd, timeout=timeout)


def spawn(cmd, logfile):
    f = open(os.path.join(SCRATCH, logfile), "w", encoding="utf-8", errors="replace")
    p = subprocess.Popen(["cmd", "/c", cmd], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT)
    return p, f


def topics():
    rc, out = sh(r"bridge\windows\ros2.bat topic list -t")
    return sorted({l.strip() for l in out.splitlines() if l.strip().startswith("/")})


def dump_map(topic, out_path, settle=6, duration=60, ascii_width=74):
    rc, text = run_env(
        "%s --topic %s --out %s --settle %d --duration %d --ascii %d"
        % (MAP_DUMP, topic, out_path, settle, duration, ascii_width))
    return rc, [l for l in text.splitlines() if l.startswith("MAPDUMP")]


# ---------------------------------------------------------------- 0. clean up
sh(r"taskkill /f /im amcl_bridge_node.exe")
sh(r"taskkill /f /im slam_bridge_node.exe")
sh(r"bridge\windows\ros2_bridge.bat stop")
time.sleep(1.5)
if os.path.isdir(GRAPH_DIR):
    for name in os.listdir(GRAPH_DIR):
        try:
            os.remove(os.path.join(GRAPH_DIR, name))
        except OSError:
            pass
    log("cleared graph dir", GRAPH_DIR)

# ------------------------------------------------- 1. daemon, /map not mirrored
rc, out = sh(r"bridge\windows\ros2_bridge.bat start --mirror /clock --mirror /odom"
             r" --mirror /scan --mirror /tf --mirror /tf_static")
log("daemon_start_rc", rc)
time.sleep(4.0)
tl = topics()
log("topics_after_daemon", tl)
log("map_mirrored_by_daemon", [t for t in tl if t.startswith("/map")])

# --------------------------------------------------------------- 2. start SLAM
slam, slamf = spawn(
    r"bridge\windows\slam_demo.bat -p map_name:=/map -p save_pose_rate:=0.0 --duration 200",
    "slam_check.log")
log("slam_pid", slam.pid)
time.sleep(12.0)
tl = topics()
log("topics_with_slam", tl)
log("map_advertised_before_driving", [t for t in tl if t.startswith("/map")])

# ------------------------------------------------------------------- 3. drive
phases = [
    ("straight", "linear.x=0.25 --rate 20 --duration 12"),
    ("left arc", "linear.x=0.2 angular.z=0.5 --rate 20 --duration 18"),
    ("right arc", "linear.x=0.3 angular.z=-0.45 --rate 20 --duration 18"),
    ("straight2", "linear.x=0.3 --rate 20 --duration 12"),
]
for name, args in phases:
    log("drive", name, args)
    rc, out = sh(r"bridge\windows\ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist " + args,
                 timeout=180)
    log("drive_rc", rc, " ".join(out.split())[-100:])

# --------------------------------------------------- 4. dump the SLAM-built map
rc, lines = dump_map("/map", r".scratch\slam_check_map.pgm")
log("slam_map_dump_rc", rc)
for line in lines:
    log(line)

slam.terminate()
time.sleep(2.0)

# ------------------------------------- 5. ground truth: mirror /map, dump again
sh(r"bridge\windows\ros2_bridge.bat stop")
time.sleep(1.5)
if os.path.isdir(GRAPH_DIR):
    for name in os.listdir(GRAPH_DIR):
        try:
            os.remove(os.path.join(GRAPH_DIR, name))
        except OSError:
            pass
rc, out = sh(r"bridge\windows\ros2_bridge.bat start")
log("daemon_restart_full_mirror_rc", rc)
time.sleep(5.0)
rc, lines = dump_map("/map", r".scratch\gt_check.pgm", duration=40)
log("gt_map_dump_rc", rc)
for line in lines:
    log(line)

# ------------------------------------------------------------- 6. uplink proof
rc, out = sh(r"bridge\windows\ros2.bat bridge status")
for line in out.splitlines():
    if any(k in line for k in ("cmd_vel", "peer=", "mirrored=", "uplink")):
        log("STATUS>", " ".join(line.split()))

log("DONE")
