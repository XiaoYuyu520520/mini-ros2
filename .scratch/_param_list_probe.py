# What does `-p map_start_pose:=[0.0,0.0,0.0]` actually put in the node?
# The reload A/B says the localisation node still logs "Map starting pose not
# specified", so the override never arrived as a double array. This prints the
# three parameters back out, and then tries the same list through
# `ros2 param set`, which takes the other parsing path.
import io, os, subprocess, sys, time

ROOT = r"E:\DesktopData\app\code\CPU"
BAT = os.path.join(ROOT, "bridge", "windows")
SC = os.path.join(ROOT, ".scratch")
BASE = "E:/DesktopData/app/code/CPU/.scratch/slam_persist"

sys.stdout.reconfigure(encoding="utf-8")
LOG = []

def say(t):
    line = "[%s] %s" % (time.strftime("%H:%M:%S"), t)
    print(line, flush=True)
    LOG.append(line)

def run(args, t=300):
    d = subprocess.run(["cmd", "/c", *args], cwd=ROOT, capture_output=True,
                       text=True, encoding="utf-8", errors="replace", timeout=t)
    return ((d.stdout or "") + (d.stderr or "")).strip()

def ros2(*a, t=120):
    return run([os.path.join(BAT, "ros2.bat"), *a], t)

say("=== daemon (default mirrors) ===")
run([os.path.join(BAT, "ros2_bridge.bat"), "start"], 240)
run(["taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
time.sleep(2)

say("=== localization node, -p map_start_pose:=[0.0,0.0,0.0] ===")
log = io.open(os.path.join(SC, "slam_param_list.log"), "w", encoding="utf-8")
p = subprocess.Popen(["cmd", "/c", os.path.join(BAT, "slam_demo.bat"),
                      "--mode", "localization",
                      "-p", "base_frame:=base_link",
                      "-p", "map_file_name:=" + BASE,
                      "-p", "map_start_pose:=[0.0,0.0,0.0]",
                      "--duration", "150"],
                     cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
time.sleep(30)
for name in ("map_file_name", "map_start_pose", "map_start_at_dock"):
    say("param get %s -> %s" % (name, ros2("param", "get", "/slam_toolbox", name, t=60)))

say("=== same list through `ros2 param set` (the other parsing path) ===")
say(ros2("param", "set", "/slam_toolbox", "map_start_pose", "[1.0,2.0,3.0]", t=60))
say("param get map_start_pose -> %s" % ros2("param", "get", "/slam_toolbox", "map_start_pose", t=60))
say("param get map_start_at_dock -> %s"
    % ros2("param", "get", "/slam_toolbox", "map_start_at_dock", t=60))

say("=== node log ===")
run(["taskkill", "/F", "/IM", "slam_bridge_node.exe"], 60)
if p.poll() is None:
    p.kill()
log.close()
txt = io.open(os.path.join(SC, "slam_param_list.log"), encoding="utf-8",
              errors="replace").read()
for l in txt.splitlines():
    if "ERROR" in l or "WARN" in l or "starting pose" in l:
        say("  " + l.strip()[:200])

io.open(os.path.join(SC, "param_list_probe.out"), "w", encoding="utf-8").write("\n".join(LOG))
print("saved .scratch\\param_list_probe.out")
