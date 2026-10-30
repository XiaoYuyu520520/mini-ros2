import os, subprocess, time

ROOT = r"E:\DesktopData\app\code\CPU"
SCRATCH = os.path.join(ROOT, ".scratch")
AMCL = (r"call bridge\windows\bridge_env.bat && "
        r"nav2\ros2_shim\build\amcl_bridge_node.exe -p base_frame_id:=base_link "
        r"-p save_pose_rate:=0.0 -p first_map_only:=true --initial-pose 0,0,0 --duration 70")


def log(*a):
    print("AMCLPROBE", *a, flush=True)


def sh(cmd, timeout=180):
    p = subprocess.run(["cmd", "/c", cmd], cwd=ROOT, capture_output=True,
                       text=True, errors="replace", timeout=timeout)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def spawn(cmd, logfile):
    f = open(os.path.join(SCRATCH, logfile), "w", encoding="utf-8", errors="replace")
    return subprocess.Popen(["cmd", "/c", cmd], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT), f


rc, out = sh(r"bridge\windows\ros2_bridge.bat status")
log("daemon_status", "running" if "ros2_bridge_daemon" in out else out.strip()[-100:])
rc, out = sh(r"bridge\windows\ros2.bat topic list")
log("topics", sorted({t.strip() for t in out.splitlines() if t.strip().startswith("/")}))

amcl, amclf = spawn(AMCL, "amcl_live4.log")
log("amcl_pid", amcl.pid)
time.sleep(10.0)

for topic in ["/tf", "/tf_static", "/scan", "/map"]:
    rc, out = sh("bridge\\windows\\ros2.bat topic info " + topic)
    log("info", topic, "|", " | ".join(l.strip() for l in out.strip().splitlines()))

time.sleep(12.0)
log("--- first 20 log lines")
with open(os.path.join(SCRATCH, "amcl_live4.log"), encoding="utf-8", errors="replace") as fh:
    lines = [l.rstrip() for l in fh if l.strip()]
for line in lines[:20]:
    log(">", line)
log("log_lines", len(lines))
tail = {}
for line in lines:
    for key in ["createLaserObject", "Couldn't transform", "amcl_pose", "Setting pose", "Received a", "subscribed"]:
        if key in line:
            tail[key] = tail.get(key, 0) + 1
log("counts", tail)
amcl.terminate()
time.sleep(1.0)
log("DONE")
